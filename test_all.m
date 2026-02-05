function test_all()
% TEST_ALL Run all test functions in the same directory as this file
%   Finds all test*.m files, runs each in a try-catch, and summarizes results.

% Get the directory where test_all.m lives
this_file = mfilename('fullpath');
this_dir = fileparts(this_file);

% Find all test*.m files in that directory
files = dir(fullfile(this_dir, 'test*.m'));
test_names = {files.name};

% Remove test_all.m from the list
test_names = test_names(~strcmp(test_names, 'test_all.m'));

% Remove .m extension to get function names
test_funcs = cellfun(@(x) x(1:end-2), test_names, 'UniformOutput', false);

num_tests = length(test_funcs);
passed = {};
failed = {};
errors = {};

fprintf('Running %d tests...\n\n', num_tests);

for i = 1:num_tests
    func_name = test_funcs{i};
    try
        feval(func_name);
        passed{end+1} = func_name; %#ok<AGROW>
        fprintf('  PASS: %s\n', func_name);
    catch ME
        failed{end+1} = func_name; %#ok<AGROW>
        errors{end+1} = ME.message; %#ok<AGROW>
        fprintf('  FAIL: %s\n', func_name);
    end
end

fprintf('\n');
fprintf('========================================\n');
fprintf('Results: %d/%d tests passed\n', length(passed), num_tests);
fprintf('========================================\n');

if isempty(failed)
    fprintf('All tests passed.\n');
else
    fprintf('\nPassed (%d):\n', length(passed));
    for i = 1:length(passed)
        fprintf('  %s\n', passed{i});
    end

    fprintf('\nFailed (%d):\n', length(failed));
    for i = 1:length(failed)
        fprintf('  %s: %s\n', failed{i}, errors{i});
    end
end

end
