<?php

declare(strict_types=1);

namespace App\Services;

use DomainException;

/** A change was asked for a session that is not running (or a PC outside its lab). Shown as HTTP 422. */
final class SessionNotActive extends DomainException {}
