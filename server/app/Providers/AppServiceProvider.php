<?php

namespace App\Providers;

use App\Services\CertificateAuthority;
use Illuminate\Support\ServiceProvider;

class AppServiceProvider extends ServiceProvider
{
    public function register(): void
    {
        $this->app->singleton(CertificateAuthority::class, fn () => CertificateAuthority::fromConfig());
    }

    public function boot(): void
    {
        //
    }
}
