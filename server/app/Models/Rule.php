<?php

declare(strict_types=1);

namespace App\Models;

use App\Models\Concerns\InvalidatesPolicies;
use Illuminate\Database\Eloquent\Model;

class Rule extends Model
{
    use InvalidatesPolicies;

    protected $fillable = ['rule_id', 'kind', 'severity', 'description', 'definition', 'enabled', 'edited', 'actions'];

    protected $casts = ['definition' => 'array', 'actions' => 'array', 'enabled' => 'boolean', 'edited' => 'boolean'];
}
