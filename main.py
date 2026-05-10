# ===========================================================================
# main.py  —  Smart Traffic Navigator  —  FastAPI Backend
# Hospital ranking + Ambulance data ingestion + Real-time WebSocket
#
# Run  : uvicorn main:app --reload --host 0.0.0.0 --port 8000
# Docs : http://localhost:8000/docs
#
# Scoring formula (matches spec and README exactly):
#   Score = (Specialization × 0.50)
#         + (Distance       × 0.35)
#         + (Beds           × 0.15)
# ===========================================================================

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect, Depends
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import create_engine, Column, Integer, String, Float, Boolean, DateTime, Text
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, Session
from pydantic import BaseModel, Field
from typing import List, Optional
from datetime import datetime
import asyncio
import json
import math
import os

# ===========================================================================
# Database setup — PostgreSQL
# Set DATABASE_URL environment variable for production
# ===========================================================================
DATABASE_URL = os.getenv(
    "DATABASE_URL",
    "postgresql://navigator:navigator@localhost:5432/smart_traffic"
)

engine       = create_engine(DATABASE_URL)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)
Base         = declarative_base()

# ===========================================================================
# Database Models
# ===========================================================================

class HospitalDB(Base):
    __tablename__ = "hospitals"

    id               = Column(Integer, primary_key=True, index=True)
    name             = Column(String(200), nullable=False)
    latitude         = Column(Float, nullable=False)
    longitude        = Column(Float, nullable=False)
    total_beds       = Column(Integer, default=100)
    available_beds   = Column(Integer, default=50)
    icu_available    = Column(Integer, default=5)
    current_load_pct = Column(Float, default=50.0)   # 0–100
    specializations  = Column(Text, default="[]")    # JSON list of strings
    is_active        = Column(Boolean, default=True)
    last_updated     = Column(DateTime, default=datetime.utcnow)


class AmbulanceDB(Base):
    __tablename__ = "ambulances"

    id             = Column(Integer, primary_key=True, index=True)
    ambulance_id   = Column(String(20), unique=True, index=True)
    status         = Column(String(30), default="idle")
    current_lat    = Column(Float, nullable=True)
    current_lng    = Column(Float, nullable=True)
    current_speed  = Column(Float, default=0.0)
    emergency_type = Column(String(50), nullable=True)
    last_updated   = Column(DateTime, default=datetime.utcnow)


class EmergencyAlertDB(Base):
    __tablename__ = "emergency_alerts"

    id             = Column(Integer, primary_key=True, index=True)
    ambulance_id   = Column(String(20), index=True)
    hospital_id    = Column(Integer, nullable=True)
    emergency_type = Column(String(50))
    patient_age    = Column(Integer, nullable=True)
    patient_gender = Column(String(10), nullable=True)
    origin_lat     = Column(Float)
    origin_lng     = Column(Float)
    status         = Column(String(30), default="active")
    created_at     = Column(DateTime, default=datetime.utcnow)
    resolved_at    = Column(DateTime, nullable=True)


Base.metadata.create_all(bind=engine)

# ===========================================================================
# Pydantic Schemas
# ===========================================================================

class AmbulanceUpdate(BaseModel):
    ambulance_id   : str
    latitude       : float
    longitude      : float
    speed          : float = 0.0
    emergency_type : Optional[str] = None
    status         : Optional[str] = "responding"


class HospitalLoad(BaseModel):
    available_beds   : int
    icu_available    : int
    current_load_pct : float = Field(..., ge=0.0, le=100.0)


class HospitalSelectRequest(BaseModel):
    ambulance_id   : str
    hospital_id    : int
    emergency_type : str
    patient_age    : Optional[int] = None
    patient_gender : Optional[str] = None
    origin_lat     : float
    origin_lng     : float


class HospitalResponse(BaseModel):
    id                   : int
    name                 : str
    distance_km          : float
    eta_minutes          : float
    score                : float
    available_beds       : int
    icu_available        : int
    current_load_pct     : float
    specializations      : List[str]
    specialization_match : bool

    class Config:
        from_attributes = True

# ===========================================================================
# Haversine distance (km)
# ===========================================================================
def haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    R    = 6371.0
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a    = (math.sin(dlat / 2) ** 2
            + math.cos(math.radians(lat1))
            * math.cos(math.radians(lat2))
            * math.sin(dlon / 2) ** 2)
    return R * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))

# ===========================================================================
# Hospital Ranking — Scoring Formula
# ===========================================================================
# Score = (Specialization × 0.50)    ← 50% weight
#       + (Distance       × 0.35)    ← 35% weight
#       + (Beds           × 0.15)    ← 15% weight
#
# Matches spec (Section 6) and README exactly.
# ===========================================================================

# Emergency type → required hospital specializations
EMERGENCY_SPECS = {
    "cardiac"    : ["Cardiology", "ICU", "CCU", "Emergency"],
    "trauma"     : ["Trauma", "Orthopedics", "Surgery", "Emergency"],
    "stroke"     : ["Neurology", "Neurosurgery", "ICU", "Emergency"],
    "burns"      : ["Burns", "Plastics", "ICU", "Emergency"],
    "respiratory": ["Pulmonology", "ICU", "Emergency"],
    "rta"        : ["Trauma", "Surgery", "Orthopedics", "Emergency"],
    "obstetric"  : ["Obstetrics", "Gynecology", "NICU", "Emergency"],
    "poisoning"  : ["Toxicology", "ICU", "Emergency"],
    "pediatric"  : ["Pediatrics", "PICU", "Emergency"],
    "psychiatric": ["Psychiatry", "Emergency"],
    "drowning"   : ["ICU", "Pulmonology", "Emergency"],
    "diabetic"   : ["Endocrinology", "ICU", "Emergency"],
}

def score_hospital(
    hospital       : HospitalDB,
    amb_lat        : float,
    amb_lng        : float,
    emergency_type : str,
    avg_speed_kmh  : float = 40.0,
    max_dist_km    : float = 20.0,
) -> Optional[HospitalResponse]:
    """Score a hospital. Returns None if no beds available."""

    if hospital.available_beds <= 0:
        return None

    # Parse specializations
    try:
        specs: List[str] = json.loads(hospital.specializations)
    except Exception:
        specs = []

    # Distance and ETA
    dist_km = haversine_km(amb_lat, amb_lng, hospital.latitude, hospital.longitude)
    eta_min = (dist_km / avg_speed_kmh) * 60.0

    # ── Component 1: Specialization match (0.0 or 1.0) ──────────
    required     = EMERGENCY_SPECS.get(emergency_type.lower(), ["Emergency"])
    spec_match   = any(s in specs for s in required)
    spec_score   = 1.0 if spec_match else 0.0

    # ── Component 2: Distance score (1.0 = closest, 0.0 = max) ──
    dist_score   = max(0.0, 1.0 - (dist_km / max_dist_km))

    # ── Component 3: Bed availability (0.0–1.0) ─────────────────
    total        = hospital.total_beds if hospital.total_beds > 0 else 1
    bed_score    = min(1.0, hospital.available_beds / total)

    # ── Final score: 50% spec + 35% distance + 15% beds ─────────
    composite = (
        0.50 * spec_score  +
        0.35 * dist_score  +
        0.15 * bed_score
    )

    return HospitalResponse(
        id                   = hospital.id,
        name                 = hospital.name,
        distance_km          = round(dist_km, 2),
        eta_minutes          = round(eta_min, 1),
        score                = round(composite, 4),
        available_beds       = hospital.available_beds,
        icu_available        = hospital.icu_available,
        current_load_pct     = hospital.current_load_pct,
        specializations      = specs,
        specialization_match = spec_match,
    )

# ===========================================================================
# WebSocket connection manager
# ===========================================================================
class ConnectionManager:
    def __init__(self):
        self.active = []

    async def connect(self, ws: WebSocket):
        await ws.accept()
        self.active.append(ws)

    def disconnect(self, ws: WebSocket):
        if ws in self.active:
            self.active.remove(ws)

    async def broadcast(self, data: dict):
        msg  = json.dumps(data)
        dead = []
        for ws in self.active:
            try:
                await ws.send_text(msg)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.disconnect(ws)

manager = ConnectionManager()

# ===========================================================================
# FastAPI app
# ===========================================================================
app = FastAPI(
    title       = "Smart Traffic Navigator — Hospital Ranking API",
    description = "Ambulance data ingestion · AI hospital ranking · Real-time GPS broadcast",
    version     = "1.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# DB dependency
def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

# ===========================================================================
# Routes
# ===========================================================================

@app.get("/health", tags=["Health"])
def health():
    return {"status": "ok", "version": "1.0.0", "timestamp": datetime.utcnow().isoformat()}


# ---------------------------------------------------------------------------
# POST /api/v1/ambulance/update
# Ambulance ESP32 sends GPS + speed + emergency type every 5 seconds
# ---------------------------------------------------------------------------
@app.post("/api/v1/ambulance/update", tags=["Ambulance"])
async def update_ambulance(payload: AmbulanceUpdate, db: Session = Depends(get_db)):
    amb = db.query(AmbulanceDB).filter(
        AmbulanceDB.ambulance_id == payload.ambulance_id
    ).first()

    if not amb:
        amb = AmbulanceDB(ambulance_id=payload.ambulance_id)
        db.add(amb)

    amb.current_lat    = payload.latitude
    amb.current_lng    = payload.longitude
    amb.current_speed  = payload.speed
    amb.emergency_type = payload.emergency_type
    amb.status         = payload.status or "responding"
    amb.last_updated   = datetime.utcnow()
    db.commit()

    # Broadcast live position to operator/hospital dashboards
    await manager.broadcast({
        "event"         : "ambulance_update",
        "ambulance_id"  : payload.ambulance_id,
        "lat"           : payload.latitude,
        "lng"           : payload.longitude,
        "speed"         : payload.speed,
        "emergency_type": payload.emergency_type,
        "status"        : amb.status,
        "timestamp"     : datetime.utcnow().isoformat(),
    })

    return {"status": "updated", "ambulance_id": payload.ambulance_id}


# ---------------------------------------------------------------------------
# GET /api/v1/hospital/ranked
# Returns AI-scored hospital list — used by EMT interface Step 3
# Query: ?lat=17.38&lng=78.48&emergency_type=cardiac&top_n=5
# ---------------------------------------------------------------------------
@app.get("/api/v1/hospital/ranked", response_model=List[HospitalResponse], tags=["Hospital"])
def get_ranked_hospitals(
    lat            : float,
    lng            : float,
    emergency_type : str,
    top_n          : int = 5,
    db             : Session = Depends(get_db),
):
    """
    Score = (Specialization × 0.50) + (Distance × 0.35) + (Beds × 0.15)
    Returns top_n hospitals sorted by composite score descending.
    """
    hospitals = db.query(HospitalDB).filter(HospitalDB.is_active == True).all()
    if not hospitals:
        raise HTTPException(status_code=404, detail="No hospitals in database")

    scored = []
    for h in hospitals:
        result = score_hospital(h, lat, lng, emergency_type)
        if result is not None:
            scored.append(result)

    scored.sort(key=lambda x: x.score, reverse=True)
    return scored[:top_n]


# ---------------------------------------------------------------------------
# POST /api/v1/ambulance/select-hospital
# EMT locks in hospital selection — sends pre-alert to hospital dashboard
# ---------------------------------------------------------------------------
@app.post("/api/v1/ambulance/select-hospital", tags=["Ambulance"])
async def select_hospital(payload: HospitalSelectRequest, db: Session = Depends(get_db)):
    hospital = db.query(HospitalDB).filter(HospitalDB.id == payload.hospital_id).first()
    if not hospital:
        raise HTTPException(status_code=404, detail="Hospital not found")

    alert = EmergencyAlertDB(
        ambulance_id   = payload.ambulance_id,
        hospital_id    = payload.hospital_id,
        emergency_type = payload.emergency_type,
        patient_age    = payload.patient_age,
        patient_gender = payload.patient_gender,
        origin_lat     = payload.origin_lat,
        origin_lng     = payload.origin_lng,
        status         = "active",
    )
    db.add(alert)
    db.commit()
    db.refresh(alert)

    await manager.broadcast({
        "event"         : "hospital_pre_alert",
        "hospital_id"   : payload.hospital_id,
        "hospital_name" : hospital.name,
        "ambulance_id"  : payload.ambulance_id,
        "emergency_type": payload.emergency_type,
        "patient_age"   : payload.patient_age,
        "patient_gender": payload.patient_gender,
        "alert_id"      : alert.id,
        "timestamp"     : datetime.utcnow().isoformat(),
    })

    return {
        "status"       : "alert_sent",
        "alert_id"     : alert.id,
        "hospital_name": hospital.name,
    }


# ---------------------------------------------------------------------------
# PATCH /api/v1/hospital/{id}/load
# Hospital dashboard updates real-time bed and occupancy data
# ---------------------------------------------------------------------------
@app.patch("/api/v1/hospital/{hospital_id}/load", tags=["Hospital"])
async def update_hospital_load(
    hospital_id : int,
    payload     : HospitalLoad,
    db          : Session = Depends(get_db),
):
    hospital = db.query(HospitalDB).filter(HospitalDB.id == hospital_id).first()
    if not hospital:
        raise HTTPException(status_code=404, detail="Hospital not found")

    hospital.available_beds   = payload.available_beds
    hospital.icu_available    = payload.icu_available
    hospital.current_load_pct = payload.current_load_pct
    hospital.last_updated     = datetime.utcnow()
    db.commit()

    await manager.broadcast({
        "event"           : "hospital_load_update",
        "hospital_id"     : hospital_id,
        "available_beds"  : payload.available_beds,
        "icu_available"   : payload.icu_available,
        "current_load_pct": payload.current_load_pct,
        "timestamp"       : datetime.utcnow().isoformat(),
    })

    return {"status": "updated", "hospital_id": hospital_id}


# ---------------------------------------------------------------------------
# WebSocket /ws/track/{code}
# Real-time GPS broadcast — hospital dashboard + operator map
# ---------------------------------------------------------------------------
@app.websocket("/ws/track/{code}")
async def websocket_track(websocket: WebSocket, code: str):
    await manager.connect(websocket)
    try:
        while True:
            await asyncio.sleep(30)
            await websocket.send_text(json.dumps({"event": "ping", "code": code}))
    except WebSocketDisconnect:
        manager.disconnect(websocket)


# ---------------------------------------------------------------------------
# GET /api/v1/hospitals  —  List all active hospitals
# ---------------------------------------------------------------------------
@app.get("/api/v1/hospitals", tags=["Hospital"])
def list_hospitals(db: Session = Depends(get_db)):
    hospitals = db.query(HospitalDB).filter(HospitalDB.is_active == True).all()
    result = []
    for h in hospitals:
        try:
            specs = json.loads(h.specializations)
        except Exception:
            specs = []
        result.append({
            "id"              : h.id,
            "name"            : h.name,
            "latitude"        : h.latitude,
            "longitude"       : h.longitude,
            "available_beds"  : h.available_beds,
            "icu_available"   : h.icu_available,
            "current_load_pct": h.current_load_pct,
            "specializations" : specs,
        })
    return result
