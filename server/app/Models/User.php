<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Factories\HasFactory;
use Illuminate\Foundation\Auth\User as Authenticatable;
use Illuminate\Notifications\Notifiable;

class User extends Authenticatable
{
    use HasFactory, Notifiable;

    public const ROLES = ['admin', 'instructor', 'assistant'];

    protected $fillable = ['name', 'email', 'password', 'role', 'active'];
    protected $attributes = ['role' => 'assistant', 'active' => true];
    protected $hidden = ['password', 'remember_token'];

    protected function casts(): array
    {
        return [
            'email_verified_at' => 'datetime',
            'last_login_at' => 'datetime',
            'password' => 'hashed',
            'active' => 'boolean',
        ];
    }

    /** Role check; admin implicitly has every role. */
    public function hasRole(string ...$roles): bool
    {
        return $this->role === 'admin' || in_array($this->role, $roles, true);
    }
}
