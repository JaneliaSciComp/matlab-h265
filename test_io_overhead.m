function test_io_overhead()
% TEST_IO_OVERHEAD Test file I/O operations
%   Throws error on failure.

% Create a test file
test_file = '/tmp/test_io_overhead.bin';
data = uint8(zeros(1024, 1024));
f = fopen(test_file, 'wb');
if f < 0
    error('test_io_overhead:fopen', 'Could not create test file');
end
fwrite(f, data, 'uint8');
fclose(f);

% Test read with open/close each time
for i = 1:10
    f = fopen(test_file, 'rb');
    assert(f > 0, 'Could not open test file for reading');
    d = fread(f, [1024, 1024], 'uint8');
    fclose(f);
    assert(numel(d) == 1024*1024, 'Read wrong number of bytes');
end

% Test read with file kept open
f = fopen(test_file, 'rb');
for i = 1:10
    fseek(f, 0, 'bof');
    d = fread(f, [1024, 1024], 'uint8');
    assert(numel(d) == 1024*1024, 'Read wrong number of bytes');
end
fclose(f);

% Cleanup
delete(test_file);

end
