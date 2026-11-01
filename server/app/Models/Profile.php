<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

/**
 * Reusable policy bundle. `network` controls the CCC layers, `agent` is merged into the Policy JSON
 * that agents receive (see App\Services\PolicyBuilder).
 */
class Profile extends Model
{
    protected $fillable = ['name', 'mode', 'description', 'network', 'agent', 'builtin'];
    protected $casts = ['network' => 'array', 'agent' => 'array', 'builtin' => 'boolean'];

    public const MODES = ['always_on', 'practice', 'test'];
    public const CATEGORIES = ['ai', 'ide', 'marketplace', 'vpn', 'custom'];
}
