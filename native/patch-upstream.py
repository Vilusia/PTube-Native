"""Apply the one audited local Tilefinch correction, idempotently."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
target = root / 'upstream/komi-tube/vendor/tilefinch/src/fetch/scheduler.inc'
before = '''    if (!success && error != NULL && error[0] != '\\0') {
        snprintf(item->result.error, sizeof(item->result.error), "%s", error);
    }'''
after = '''    if (!success && error != NULL && error[0] != '\\0') {
        /* PTube: error often points at result.error itself. snprintf's
           source and destination must not overlap (glibc erases it). */
        size_t length = strlen(error);
        if (length >= sizeof(item->result.error))
            length = sizeof(item->result.error) - 1u;
        memmove(item->result.error, error, length);
        item->result.error[length] = '\\0';
    }'''
source = target.read_text()
if after in source:
    print('Scheduler error-overlap patch already applied.')
elif source.count(before) == 1:
    target.write_text(source.replace(before, after))
    print('Applied scheduler error-overlap patch.')
else:
    raise SystemExit('Upstream scheduler no longer matches the audited patch')
