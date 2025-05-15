from flask import Blueprint, jsonify
from datetime import datetime

from app import db
from app.services.auth import require_api_key
from app.models import AttendanceRecord

last_event_bp = Blueprint('last_event', __name__)

@last_event_bp.route('/last_event/<device_id>', methods=['GET'])
@require_api_key
def last_event(device_id):
    print(1)
    last_record = AttendanceRecord.query.filter_by(device_id=device_id) \
        .order_by(AttendanceRecord.timestamp.desc()) \
        .first()
    print(last_record)
    if last_record:
        return jsonify({
            'device_id': device_id,
            'last_event': last_record.event_type,
            'timestamp': last_record.timestamp.isoformat()
        })
    else:
        return jsonify({
            'device_id': device_id,
            'last_event': None,
            'message': 'No records found'
        })
