from flask import Blueprint, request, jsonify
from app.models import AttendanceRecord
from app import db
from app.services.auth import require_api_key

attendance_bp = Blueprint('attendance', __name__)

# Эндпоинт для регистрации событий (только POST)
@attendance_bp.route('/attendance', methods=['POST'])
@require_api_key
def register_attendance():
    data = request.json

    if not all(key in data for key in ['device_id', 'event_type']):
        return jsonify({'error': 'Missing required fields'}), 400

    if data['event_type'] not in ('in', 'out'):
        return jsonify({'error': 'Invalid event_type'}), 400

    new_record = AttendanceRecord(
        device_id=data['device_id'],
        event_type=data['event_type'],
        location=data.get('location')
    )

    db.session.add(new_record)
    db.session.commit()

    return jsonify({
        'status': 'success',
        'record_id': new_record.id,
        'timestamp': new_record.timestamp.isoformat()
    }), 201

# Эндпоинт для получения истории (только GET)
@attendance_bp.route('/attendance/<device_id>', methods=['GET'])  # Убрали POST
@require_api_key  # Добавили аутентификацию
def get_device_records(device_id):
    records = AttendanceRecord.query.filter_by(device_id=device_id)\
                                  .order_by(AttendanceRecord.timestamp.desc())\
                                  .limit(10).all()

    return jsonify({
        'device_id': device_id,
        'records': [{
            'id': r.id,
            'event_type': r.event_type,
            'timestamp': r.timestamp.isoformat(),
            'location': r.location
        } for r in records]
    })