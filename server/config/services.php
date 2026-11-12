<?php

return [

    /*
    |--------------------------------------------------------------------------
    | Third Party Services
    |--------------------------------------------------------------------------
    |
    | This file is for storing the credentials for third party services such
    | as Resend, Postmark, AWS, and more. This file provides the de facto
    | location for this type of information, allowing packages to have
    | a conventional file to locate the various service credentials.
    |
    */

    'postmark' => [
        'key' => env('POSTMARK_API_KEY'),
    ],

    'resend' => [
        'key' => env('RESEND_API_KEY'),
    ],

    'ses' => [
        'key' => env('AWS_ACCESS_KEY_ID'),
        'secret' => env('AWS_SECRET_ACCESS_KEY'),
        'region' => env('AWS_DEFAULT_REGION', 'us-east-1'),
    ],

    'slack' => [
        'notifications' => [
            'bot_user_oauth_token' => env('SLACK_BOT_USER_OAUTH_TOKEN'),
            'channel' => env('SLACK_BOT_USER_DEFAULT_CHANNEL'),
        ],
    ],

    'screen_gateway' => [
        'secret' => env('SCREEN_GATEWAY_SECRET') ?: (env('APP_KEY') ? hash_hmac('sha256', 'screen-gateway', (string) env('APP_KEY')) : ''),
        'agent_secret' => env('SCREEN_GATEWAY_AGENT_SECRET') ?: (env('APP_KEY') ? hash_hmac('sha256', 'screen-gateway-agent', (string) env('APP_KEY')) : ''),
        'issuer' => env('SCREEN_GATEWAY_ISSUER', 'xephyguard-ccc'),
        'audience' => env('SCREEN_GATEWAY_AUDIENCE', 'xephyguard-gateway'),
        'url' => env('SCREEN_GATEWAY_URL', ''),
        'path' => env('SCREEN_GATEWAY_PATH', '/screen-gw'),
        'port' => (int) env('SCREEN_GATEWAY_PORT', 8082),
        'ttl' => (int) env('SCREEN_GATEWAY_TTL', 30),
    ],

];
