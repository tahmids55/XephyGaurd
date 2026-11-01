# Laravel CCC image: one image, several roles (web | reverb | worker | scheduler | artisan ...).
FROM node:22-bookworm-slim AS assets
WORKDIR /src
COPY server/package.json server/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY server/ ./
RUN npm run build

FROM composer:2 AS vendor
WORKDIR /src
COPY server/composer.json server/composer.lock ./
RUN composer install --no-dev --no-scripts --no-interaction --prefer-dist --ignore-platform-req=ext-pcntl

FROM php:8.3-fpm-bookworm
RUN apt-get update && apt-get install -y --no-install-recommends nginx supervisor libicu-dev libzip-dev libpng-dev \
        default-mysql-client openssl unzip \
    && docker-php-ext-install -j"$(nproc)" pdo_mysql intl zip pcntl gd opcache \
    && rm -rf /var/lib/apt/lists/* /etc/nginx/sites-enabled/default
COPY ccc-deploy/docker/php.ini /usr/local/etc/php/conf.d/xephyguard.ini
COPY ccc-deploy/docker/php-fpm-pool.conf /usr/local/etc/php-fpm.d/zz-xephyguard.conf
COPY ccc-deploy/docker/supervisord.conf /etc/supervisor/conf.d/xephyguard.conf
COPY ccc-deploy/docker/entrypoint.sh /usr/local/bin/xg-entrypoint
WORKDIR /var/www/html
COPY server/ ./
COPY data/ /opt/xephyguard/data/
COPY --from=vendor /src/vendor ./vendor
COPY --from=assets /src/public/build ./public/build
RUN rm -f public/hot && php artisan package:discover --ansi \
    && chown -R www-data:www-data storage bootstrap/cache && chmod +x /usr/local/bin/xg-entrypoint
ENV XG_DATA_DIR=/opt/xephyguard/data
ENTRYPOINT ["xg-entrypoint"]
CMD ["web"]
