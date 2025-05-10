from functools import wraps
from flask import request, jsonify

API_KEYS = {
    "ESP8266_01": "secret-key-123",
    "ESP8266_02": "secret-key-456"
}


def require_api_key(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        api_key = request.headers.get('X-API-KEY')
        if not api_key:
            return jsonify({'error': 'API key is missing'}), 401

        device_id = request.json.get('device_id') if request.json else None
        if not device_id or API_KEYS.get(device_id) != api_key:
            return jsonify({'error': 'Invalid API key'}), 403

        return f(*args, **kwargs)

    return decorated_function