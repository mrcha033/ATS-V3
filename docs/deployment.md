# Deployment Guide

ATS-V3 is designed for easy deployment using Docker and Docker Compose, which encapsulates all services and their dependencies into portable containers. This guide outlines the steps to deploy the entire system.

## Prerequisites

-   **Docker**: Ensure Docker Engine is installed and running on your deployment server.
-   **Docker Compose**: Ensure Docker Compose is installed.

## Recommended Deployment: Docker Compose

The `docker-compose.yml` file in the project root defines all the services required to run ATS-V3, including:

-   `ats-v3`: The main application container, built from the `Dockerfile`.
-   `redis`: An in-memory data store and message broker.
-   `influxdb`: A time-series database for market data, metrics, and logs.
-   `prometheus`: A monitoring system that scrapes metrics from `ats-v3` and other services.
-   `grafana`: A data visualization and dashboarding tool for Prometheus and InfluxDB.

### Steps to Deploy

1.  **Clone the Repository**:
    ```bash
    git clone https://github.com/your-repo/ATS-V3.git
    cd ATS-V3
    ```

2.  **Configure `settings.json`**:
    Before starting the containers, you need to configure the `settings.json` file with your exchange API keys, database credentials, and other operational parameters. This file is mounted into the `ats-v3` container.
    ```bash
    cp config/settings.json.example config/settings.json
    # Edit config/settings.json with your actual settings
    vim config/settings.json
    ```
    **Important**: For production environments, consider using environment variables or Docker secrets for sensitive information instead of directly in `settings.json`.

3.  **Start the Services**:
    Navigate to the project root directory (where `docker-compose.yml` is located) and run:
    ```bash
    docker-compose up -d
    ```
    -   `up`: Builds, creates, and starts the services.
    -   `-d`: Runs the containers in detached mode (in the background).

4.  **Verify Deployment**:
    Check if all containers are running:
    ```bash
    docker-compose ps
    ```
    You should see `Up` status for all services.

5.  **Access Dashboards**:
    -   **Grafana**: Accessible at `http://localhost:3000` (default user: `admin`, password: `admin`). You will need to configure data sources (Prometheus, InfluxDB) and import dashboards.
    -   **Prometheus**: Accessible at `http://localhost:9090`.

## Managing the Deployment

-   **Stop Services**:
    ```bash
    docker-compose stop
    ```

-   **Stop and Remove Services (and volumes)**:
    ```bash
    docker-compose down -v
    ```
    -   `down`: Stops and removes containers, networks, and images.
    -   `-v`: Also removes named volumes declared in the `volumes` section of the `docker-compose.yml`. Use with caution as this will delete your Redis, InfluxDB, Prometheus, and Grafana data.

-   **View Logs**:
    ```bash
    docker-compose logs -f
    ```
    -   `-f`: Follows log output.

-   **Rebuild Services**:
    If you make changes to the `Dockerfile` or the application code, you need to rebuild the `ats-v3` service:
    ```bash
    docker-compose build ats-v3
    docker-compose up -d
    ```

## Production Deployment Considerations

-   **Security**: 
    -   Do not expose sensitive ports (e.g., Redis 6379, InfluxDB 8086) directly to the internet.
    -   Use strong, unique passwords for InfluxDB and Grafana.
    -   Implement proper firewall rules.
    -   Consider using Docker secrets for API keys and other sensitive credentials.
-   **Scalability**: For high-load production environments, consider orchestrators like Kubernetes for advanced scaling, self-healing, and management capabilities.
-   **High Availability**: Implement strategies for redundant services and data replication.
-   **Persistent Storage**: Ensure Docker volumes are properly backed up.
-   **Monitoring & Alerting**: Configure Prometheus alerts and integrate them with your preferred notification channels (e.g., PagerDuty, Slack, Email via `notification_service`).

