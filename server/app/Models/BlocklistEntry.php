<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class BlocklistEntry extends Model
{
    protected $fillable = ['category', 'value', 'kind', 'source', 'note', 'enabled', 'created_by'];
    protected $casts = ['enabled' => 'boolean'];

    public const CATEGORIES = ['ai', 'ide', 'marketplace', 'doh', 'vpn', 'custom'];
}
