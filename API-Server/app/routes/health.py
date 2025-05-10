from flask import Blueprint, jsonify
from datetime import datetime

health_bp = Blueprint('health', __name__)

@health_bp.route('/health')
def health_check():
    print(1)
    return jsonify({
        'status': 'healthy',
        'timestamp': datetime.utcnow().isoformat()
    })