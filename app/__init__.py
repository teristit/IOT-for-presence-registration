from flask import Flask
from flask_sqlalchemy import SQLAlchemy
from flask_migrate import Migrate

db = SQLAlchemy()
migrate = Migrate()


def create_app():
    app = Flask(__name__)
    app.config.from_object('app.utils.config.Config')

    db.init_app(app)
    migrate.init_app(app, db)

    from app.routes.attendance import attendance_bp
    from app.routes.health import health_bp
    from app.routes.last_event import last_event_bp

    app.register_blueprint(attendance_bp, url_prefix='/api')
    app.register_blueprint(health_bp, url_prefix='/api')
    app.register_blueprint(last_event_bp, url_prefix='/api')

    return app