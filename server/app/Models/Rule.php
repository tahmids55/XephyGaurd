<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Rule extends Model
{
    protected $fillable = ['rule_id', 'kind', 'severity', 'description', 'definition', 'enabled', 'actions'];
    protected $casts = ['definition' => 'array', 'actions' => 'array', 'enabled' => 'boolean'];
}
