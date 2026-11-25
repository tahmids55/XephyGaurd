<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Student;
use App\Services\RosterImport;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Http\UploadedFile;
use InvalidArgumentException;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-42 (I-SERVER-31): header columns by name with `!== false`, row-length checks, reasons for skipped rows. */
class RosterImportTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_header_column_at_index_zero_and_columns_beyond_three(): void
    {
        // `name` at index 0 was read from index 1 by the old `?:` mapping; `login` at index 4 is past column 3.
        $r = RosterImport::parse("\xEF\xBB\xBFname,Email,Student_ID,group,LOGIN\nAnn,a@x,1,g1,ANN\nBob,b@x,2,g2\n");
        $this->assertSame([['student_number' => '1', 'name' => 'Ann', 'login' => 'ann']], $r->rows);
        $this->assertSame([['line' => 3, 'reason' => 'too few columns (need 5)']], $r->skipped);
    }

    public function test_rows_without_header_and_skip_reasons(): void
    {
        $long = str_repeat('x', RosterImport::MAX_LEN + 1);
        $r = RosterImport::parse("1,Ann,ann\n\n,NoId,x\n3,NoLogin,\n4,$long,l4\n5,Short\n6,\"Doe, Jo\",jo\n");
        $this->assertSame(['1', '6'], array_column($r->rows, 'student_number'));
        $this->assertSame('Doe, Jo', $r->rows[1]['name']);
        $this->assertSame([
            ['line' => 3, 'reason' => 'missing student id'],
            ['line' => 4, 'reason' => 'missing login'],
            ['line' => 5, 'reason' => 'value longer than 255 characters'],
            ['line' => 6, 'reason' => 'too few columns (need 3)'],
        ], $r->skipped);
    }

    public function test_header_without_a_required_column_is_rejected(): void
    {
        $this->expectException(InvalidArgumentException::class);
        $this->expectExceptionMessage('Header is missing: name.');
        RosterImport::parse("student_id,login\n1,ann\n");
    }

    public function test_upload_reports_skipped_rows_and_rejects_bad_header(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('instructor'));
        $f = fn (string $body) => UploadedFile::fake()->createWithContent('r.csv', $body);

        $this->post('/roster/import', ['file' => $f("login,name,id\nann,Ann,1\nbob,Bob\n")])
            ->assertRedirect()->assertSessionHas('status', '1 students imported/updated. 1 rows skipped (line 3: too few columns (need 3)).');
        $this->assertSame('1', Student::sole()->student_number);

        $this->post('/roster/import', ['file' => $f("id,login\n2,zed\n")])->assertSessionHasErrors('file');
        $this->assertSame(1, Student::count());
    }
}
