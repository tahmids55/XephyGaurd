#!/bin/sh
set -e
cd /var/www/html
mkdir -p storage/app/private storage/app/public/downloads storage/framework/cache storage/framework/sessions storage/framework/views storage/logs /var/lib/xephyguard/net
chown -R www-data:www-data storage /var/lib/xephyguard/net
case "$1" in
  web)
    php artisan config:cache && php artisan route:cache && php artisan view:cache
    php artisan migrate --force
    exec supervisord -n -c /etc/supervisor/supervisord.conf ;;
  reverb)    exec su -s /bin/sh www-data -c "php artisan reverb:start --host=127.0.0.1 --port=8081" ;;
  worker)    exec su -s /bin/sh www-data -c "php artisan queue:work --sleep=1 --tries=3 --max-time=3600" ;;
  scheduler) exec su -s /bin/sh www-data -c "php artisan schedule:work" ;;
  artisan)   shift; exec php artisan "$@" ;;
  *)         exec "$@" ;;
esac
