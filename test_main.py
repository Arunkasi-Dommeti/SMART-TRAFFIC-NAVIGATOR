# ===========================================================================
# test_main.py — Smart Traffic Navigator — pytest Unit Tests
#
# Run : pytest test_main.py -v
# Deps: pip install pytest pytest-asyncio httpx fastapi sqlalchemy
#
# Coverage:
#   ✓ haversine_km          — distance accuracy
#   ✓ score_hospital()      — scoring formula, edge cases, no-bed guard
#   ✓ calculate_survival_boost() — formula correctness, cap, unknown type
#   ✓ ConnectionManager     — connect, disconnect, broadcast, dead-socket cleanup
#   ✓ /health endpoint      — API smoke test
#   ✓ /api/v1/survival-boost — query param routing
# ===========================================================================

import json
import os
import asyncio
import pytest
from unittest.mock import AsyncMock, MagicMock, patch
from datetime import datetime, timezone

# ── Env vars must be set BEFORE importing the app ───────────────────────────
os.environ.setdefault("DATABASE_URL", "sqlite:///:memory:")
os.environ.setdefault("HOSPITAL_SECURE_KEY", "test-key-for-pytest")
os.environ.setdefault("GEMINI_API_KEY", "test-gemini-key")

from fastapi.testclient import TestClient
from main import (
    app,
    haversine_km,
    score_hospital,
    calculate_survival_boost,
    ConnectionManager,
    HospitalDB,
    EMERGENCY_SPECS,
    SURVIVAL_PARAMS,
)

client = TestClient(app)


# ===========================================================================
# Fixtures
# ===========================================================================

def make_hospital(
    id=1,
    name="Test Hospital",
    lat=17.385,
    lng=78.487,
    total_beds=100,
    available_beds=50,
    icu_available=10,
    current_load_pct=50.0,
    specializations=None,
    is_active=True,
):
    """Factory for HospitalDB instances (no DB required)."""
    h = HospitalDB()
    h.id               = id
    h.name             = name
    h.latitude         = lat
    h.longitude        = lng
    h.total_beds       = total_beds
    h.available_beds   = available_beds
    h.icu_available    = icu_available
    h.current_load_pct = current_load_pct
    h.specializations  = json.dumps(specializations or ["Emergency", "ICU"])
    h.is_active        = is_active
    h.last_updated     = datetime.now(timezone.utc)
    return h


# ===========================================================================
# 1. haversine_km — distance accuracy
# ===========================================================================

class TestHaversineKm:
    def test_same_point_is_zero(self):
        assert haversine_km(17.385, 78.487, 17.385, 78.487) == 0.0

    def test_known_distance_approx(self):
        # Hyderabad → Secunderabad (~10 km direct)
        dist = haversine_km(17.385, 78.487, 17.443, 78.498)
        assert 6.0 < dist < 14.0, f"Expected ~10 km, got {dist:.2f}"

    def test_symmetry(self):
        d1 = haversine_km(17.0, 78.0, 18.0, 79.0)
        d2 = haversine_km(18.0, 79.0, 17.0, 78.0)
        assert abs(d1 - d2) < 0.001

    def test_positive_result(self):
        assert haversine_km(0.0, 0.0, 1.0, 1.0) > 0.0


# ===========================================================================
# 2. score_hospital() — core ranking algorithm
# ===========================================================================

class TestScoreHospital:
    def test_returns_none_when_no_beds(self):
        h = make_hospital(available_beds=0)
        result = score_hospital(h, 17.385, 78.487, "cardiac")
        assert result is None, "Should return None when available_beds=0"

    def test_spec_match_gives_higher_score(self):
        """Hospital with matching specialization must score higher than one without."""
        # Cardiac emergency → needs Cardiology
        h_match    = make_hospital(id=1, specializations=["Cardiology", "ICU"])
        h_no_match = make_hospital(id=2, specializations=["Orthopedics"])

        r_match    = score_hospital(h_match,    17.385, 78.487, "cardiac")
        r_no_match = score_hospital(h_no_match, 17.385, 78.487, "cardiac")

        assert r_match is not None
        assert r_no_match is not None
        assert r_match.score > r_no_match.score, (
            f"Spec-matched ({r_match.score}) should beat non-matched ({r_no_match.score})"
        )

    def test_score_weights_sum_to_1(self):
        """
        Verify formula: 0.50 + 0.35 + 0.15 = 1.0
        Perfect case: spec match + zero distance + all beds available → score = 1.0
        """
        h = make_hospital(
            lat=17.385, lng=78.487,   # Same coords as ambulance → dist=0
            total_beds=100, available_beds=100,
            specializations=["Cardiology", "ICU"],
        )
        r = score_hospital(h, 17.385, 78.487, "cardiac")
        assert r is not None
        assert abs(r.score - 1.0) < 0.01, f"Perfect hospital should score ~1.0, got {r.score}"

    def test_score_range_0_to_1(self):
        h = make_hospital()
        r = score_hospital(h, 17.385, 78.487, "trauma")
        assert r is not None
        assert 0.0 <= r.score <= 1.0, f"Score out of range: {r.score}"

    def test_distance_affects_score(self):
        """Closer hospital should score higher (all else equal, no spec match)."""
        h_near = make_hospital(id=1, lat=17.386, lng=78.488)   # ~0.1 km away
        h_far  = make_hospital(id=2, lat=17.500, lng=78.600)   # ~18 km away

        r_near = score_hospital(h_near, 17.385, 78.487, "trauma")
        r_far  = score_hospital(h_far,  17.385, 78.487, "trauma")

        assert r_near is not None
        assert r_far  is not None
        assert r_near.score > r_far.score

    def test_eta_minutes_positive(self):
        h = make_hospital(lat=17.443, lng=78.498)
        r = score_hospital(h, 17.385, 78.487, "cardiac")
        assert r is not None
        assert r.eta_minutes > 0.0

    def test_unknown_emergency_type_falls_back_to_emergency(self):
        """Unknown emergency type should still produce a valid result."""
        h = make_hospital(specializations=["Emergency"])
        r = score_hospital(h, 17.385, 78.487, "unknown_type_xyz")
        assert r is not None
        assert r.score >= 0.0

    def test_malformed_specializations_json_handled(self):
        """Corrupt specializations JSON must not crash — should treat as []."""
        h = make_hospital()
        h.specializations = "NOT_VALID_JSON"
        r = score_hospital(h, 17.385, 78.487, "cardiac")
        assert r is not None  # Must not raise

    def test_specialization_match_flag(self):
        h_match    = make_hospital(specializations=["Neurology", "ICU"])
        h_no_match = make_hospital(specializations=["Dermatology"])

        r_match    = score_hospital(h_match,    17.385, 78.487, "stroke")
        r_no_match = score_hospital(h_no_match, 17.385, 78.487, "stroke")

        assert r_match.specialization_match    is True
        assert r_no_match.specialization_match is False

    def test_bed_score_partial(self):
        """Hospital with 25/100 beds scores lower than 100/100."""
        h_low  = make_hospital(total_beds=100, available_beds=25)
        h_high = make_hospital(total_beds=100, available_beds=100)

        r_low  = score_hospital(h_low,  17.385, 78.487, "cardiac")
        r_high = score_hospital(h_high, 17.385, 78.487, "cardiac")

        assert r_high.score > r_low.score


# ===========================================================================
# 3. calculate_survival_boost() — formula + caps
# ===========================================================================

class TestCalculateSurvivalBoost:
    def test_cardiac_2_junctions(self):
        """
        Cardiac: 9%/min × (2 × 2.5 min) = 45% boost
        """
        r = calculate_survival_boost("cardiac", junctions_cleared=2)
        assert r["boost_pct"] == 45.0, f"Expected 45.0, got {r['boost_pct']}"
        assert r["time_saved_minutes"] == 5.0
        assert r["junctions_cleared"] == 2

    def test_cap_at_50_percent(self):
        """High junctions cleared should never exceed 50% cap."""
        r = calculate_survival_boost("cardiac", junctions_cleared=10)
        assert r["boost_pct"] <= 50.0, f"Boost exceeded cap: {r['boost_pct']}"

    def test_zero_junctions(self):
        r = calculate_survival_boost("cardiac", junctions_cleared=0)
        assert r["boost_pct"] == 0.0

    def test_all_known_emergency_types(self):
        """All types in SURVIVAL_PARAMS must return valid results."""
        for etype in SURVIVAL_PARAMS:
            r = calculate_survival_boost(etype, junctions_cleared=2)
            assert 0.0 <= r["boost_pct"] <= 50.0, f"Out of range for {etype}: {r['boost_pct']}"
            assert "clinical_source" in r
            assert "formula" in r

    def test_unknown_type_uses_default(self):
        """Unknown emergency type should use default params, not crash."""
        r = calculate_survival_boost("unknown_emergency", junctions_cleared=2)
        assert r is not None
        assert r["boost_pct"] >= 0.0

    def test_label_format(self):
        r = calculate_survival_boost("stroke", junctions_cleared=2)
        assert r["label"].startswith("+")
        assert "%" in r["label"]

    def test_single_junction(self):
        """stroke: 6%/min × (1 × 2.0 min) = 12%"""
        r = calculate_survival_boost("stroke", junctions_cleared=1)
        assert r["boost_pct"] == 12.0, f"Expected 12.0, got {r['boost_pct']}"

    def test_result_keys_present(self):
        r = calculate_survival_boost("trauma", junctions_cleared=2)
        required_keys = [
            "boost_pct", "label", "formula",
            "time_saved_minutes", "junctions_cleared",
            "clinical_source", "methodology"
        ]
        for key in required_keys:
            assert key in r, f"Missing key: {key}"


# ===========================================================================
# 4. ConnectionManager — WebSocket lifecycle
# ===========================================================================

class TestConnectionManager:
    def setup_method(self):
        self.mgr = ConnectionManager()

    def _mock_ws(self):
        ws = AsyncMock()
        ws.accept = AsyncMock()
        ws.send_text = AsyncMock()
        return ws

    def test_initial_state_empty(self):
        assert self.mgr.active == []

    @pytest.mark.asyncio
    async def test_connect_adds_to_active(self):
        ws = self._mock_ws()
        await self.mgr.connect(ws)
        assert ws in self.mgr.active
        ws.accept.assert_called_once()

    def test_disconnect_removes_from_active(self):
        ws = self._mock_ws()
        self.mgr.active.append(ws)
        self.mgr.disconnect(ws)
        assert ws not in self.mgr.active

    def test_disconnect_nonexistent_is_safe(self):
        ws = self._mock_ws()
        # Should not raise even if ws was never added
        self.mgr.disconnect(ws)

    @pytest.mark.asyncio
    async def test_broadcast_sends_to_all(self):
        ws1, ws2 = self._mock_ws(), self._mock_ws()
        self.mgr.active = [ws1, ws2]
        await self.mgr.broadcast({"event": "test", "value": 42})

        expected = json.dumps({"event": "test", "value": 42})
        ws1.send_text.assert_called_once_with(expected)
        ws2.send_text.assert_called_once_with(expected)

    @pytest.mark.asyncio
    async def test_broadcast_removes_dead_connections(self):
        """Dead WebSocket (send_text raises) must be cleaned up after broadcast."""
        ws_alive = self._mock_ws()
        ws_dead  = self._mock_ws()
        ws_dead.send_text.side_effect = Exception("Connection closed")

        self.mgr.active = [ws_alive, ws_dead]
        await self.mgr.broadcast({"event": "ping"})

        assert ws_alive in self.mgr.active
        assert ws_dead  not in self.mgr.active

    @pytest.mark.asyncio
    async def test_broadcast_empty_list_is_safe(self):
        # Should not raise
        await self.mgr.broadcast({"event": "ping"})

    @pytest.mark.asyncio
    async def test_multiple_connects(self):
        ws1, ws2, ws3 = self._mock_ws(), self._mock_ws(), self._mock_ws()
        await self.mgr.connect(ws1)
        await self.mgr.connect(ws2)
        await self.mgr.connect(ws3)
        assert len(self.mgr.active) == 3


# ===========================================================================
# 5. API endpoint smoke tests
# ===========================================================================

class TestAPIEndpoints:
    def test_health_returns_ok(self):
        r = client.get("/health")
        assert r.status_code == 200
        data = r.json()
        assert data["status"] == "ok"
        assert "timestamp" in data

    def test_survival_boost_cardiac(self):
        r = client.get("/api/v1/survival-boost?emergency_type=cardiac&junctions_cleared=2")
        assert r.status_code == 200
        data = r.json()
        assert data["boost_pct"] == 45.0

    def test_survival_boost_missing_param(self):
        r = client.get("/api/v1/survival-boost")
        # emergency_type is required → 422 Unprocessable Entity
        assert r.status_code == 422

    def test_ranked_hospitals_no_db_returns_404(self):
        """With empty SQLite test DB, should return 404 not 500."""
        r = client.get(
            "/api/v1/hospital/ranked"
            "?lat=17.385&lng=78.487&emergency_type=cardiac&top_n=5"
        )
        assert r.status_code in (200, 404), f"Unexpected status: {r.status_code}"

    def test_hospital_load_update_requires_api_key(self):
        r = client.patch(
            "/api/v1/hospital/1/load",
            json={"available_beds": 20, "icu_available": 5, "current_load_pct": 60.0},
        )
        # No API key header → 401 (APIKeyHeader auto_error=True) or 422
        assert r.status_code in (401, 403, 422)

    def test_hospital_load_update_wrong_key(self):
        r = client.patch(
            "/api/v1/hospital/1/load",
            json={"available_beds": 20, "icu_available": 5, "current_load_pct": 60.0},
            headers={"X-Hospital-API-Key": "wrong-key"},
        )
        assert r.status_code == 403


# ===========================================================================
# 6. EMERGENCY_SPECS completeness
# ===========================================================================

class TestEmergencySpecs:
    def test_all_types_have_emergency_fallback(self):
        """Every emergency type must include 'Emergency' as a required spec."""
        for etype, specs in EMERGENCY_SPECS.items():
            assert "Emergency" in specs, (
                f"'{etype}' missing 'Emergency' specialization — "
                "hospitals without it will never match"
            )

    def test_cardiac_has_icu(self):
        assert "ICU" in EMERGENCY_SPECS["cardiac"]

    def test_stroke_has_neurology(self):
        assert "Neurology" in EMERGENCY_SPECS["stroke"]
