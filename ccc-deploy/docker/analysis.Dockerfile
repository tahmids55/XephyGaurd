FROM python:3.12-slim
WORKDIR /opt/xephyguard/analysis
COPY analysis/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY analysis/xg_analysis ./xg_analysis
ENV XG_ANALYSIS_PORT=8090
USER nobody
CMD ["python", "-m", "xg_analysis", "serve"]
