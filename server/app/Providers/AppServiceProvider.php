<?php

namespace App\Providers;

use App\Services\CertificateAuthority;
use App\View\Composers\OpenAlertsComposer;
use Illuminate\Support\Facades\View;
use Illuminate\Support\ServiceProvider;

class AppServiceProvider extends ServiceProvider
{
    public function register(): void
    {
        $this->app->singleton(CertificateAuthority::class, fn () => CertificateAuthority::fromConfig());
    }

    public function boot(): void
    {
        View::composer('layouts.app', OpenAlertsComposer::class);
    }
}
