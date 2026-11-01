<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Support\Facades\Cache;

/** Key/value settings editable in the dashboard; values fall back to config/xephyguard.php. */
class Setting extends Model
{
    protected $primaryKey = 'key';
    public $incrementing = false;
    protected $keyType = 'string';
    protected $fillable = ['key', 'value'];
    protected $casts = ['value' => 'json'];

    public static function get(string $key, mixed $default = null): mixed
    {
        $all = Cache::remember('xg.settings', 60, fn () => static::query()->pluck('value', 'key')->all());
        if (array_key_exists($key, $all)) {
            return $all[$key];
        }

        return $default ?? config("xephyguard.$key");
    }

    public static function put(string $key, mixed $value): void
    {
        static::query()->updateOrCreate(['key' => $key], ['value' => $value]);
        Cache::forget('xg.settings');
    }
}
