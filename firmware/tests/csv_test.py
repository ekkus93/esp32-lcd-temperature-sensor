"""Validate production CSV output with an independent RFC 4180 reader."""
import csv
import io
import subprocess
import sys

text = subprocess.check_output([sys.argv[1]], text=True, encoding='utf-8')
rows = list(csv.DictReader(io.StringIO(text, newline='')))
assert len(rows) == 5 and all(len(row) == 17 for row in rows)
assert all(row['simulated'] == '1' and row['units'] == 'C' for row in rows)
assert all(row['boot_id'] == '0000000000000007' for row in rows)
assert rows[0]['sensor_name'] == 'Cold, "A"'
assert rows[0]['average_c'] == '-10.123' and rows[0]['minimum_c'] == '-20.000'
assert rows[0]['utc_seconds'] == '' and rows[0]['timestamp_quality'] == 'boot_uptime'
assert rows[1]['sensor_name'] == "' =1+1" and rows[1]['name_escaped'] == '1'
assert rows[2]['sensor_name'] == '' and rows[2]['name_source'] == 'unknown'
assert rows[3]['average_c'] == rows[3]['minimum_c'] == rows[3]['maximum_c'] == ''
assert rows[3]['valid_count'] == '0' and rows[3]['state_flags'] == '0010'
assert rows[4]['utc_seconds'] == '1800000000' and rows[4]['sensor_name'] == 'Café'
print('PASS: CSV quoting, UTF-8, simulated marker, formula escaping, unknown time/name and empty values')
