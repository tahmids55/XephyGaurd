FROM python:3.12-slim
COPY ccc-deploy/services/discovery.py /opt/xephyguard/discovery.py
USER nobody
CMD ["python", "/opt/xephyguard/discovery.py"]
