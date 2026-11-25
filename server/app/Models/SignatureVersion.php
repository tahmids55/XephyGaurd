<?php

declare(strict_types=1);

namespace App\Models;

use App\Models\Concerns\InvalidatesPolicies;
use Illuminate\Database\Eloquent\Model;

class SignatureVersion extends Model
{
    use InvalidatesPolicies;

    protected $fillable = ['version', 'bundle', 'signature', 'sha256', 'created_by', 'note', 'active'];

    protected $casts = ['active' => 'boolean'];

    public static function active(): ?self
    {
        return static::query()->where('active', true)->latest('id')->first();
    }

    public function envelope(): array
    {
        return ['version' => $this->version, 'bundle' => $this->bundle, 'signature' => $this->signature];
    }
}
