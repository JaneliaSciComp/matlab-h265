function build(target)
% BUILD Build MEX files for the h265 package
%   h265.build()          - Build out-of-date MEX files
%   h265.build('clean')   - Delete all MEX files
%   h265.build('rebuild') - Clean and rebuild all

if nargin < 1
  target = '';
end

% Get the directory where this file lives
this_dir = fileparts(mfilename('fullpath'));

% Build the make command
if isempty(target)
  command = sprintf('make -C "%s"', this_dir);
else
  command = sprintf('make -C "%s" %s', this_dir, target);
end

% Run make
[status, output] = system(command);
if status ~= 0
  error('h265:build', 'Build failed:\n%s', output);
end

if ~isempty(output)
  fprintf('%s', output);
end

end
