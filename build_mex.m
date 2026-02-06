function build_mex(varargin)
% BUILD_MEX Compile all MEX functions for H.265 video reading and writing
%
% Only rebuilds files where the source is newer than the MEX file.
%
% Usage:
%   build_mex            - Build out-of-date MEX files
%   build_mex --rebuild  - Delete all MEX files, then rebuild all
%   build_mex --clean    - Delete all MEX files

% Parse arguments to determine what to do
if nargin == 0
  do_clean = false;
  do_build = true;
elseif strcmp(varargin{1}, '--clean')
  do_clean = true;
  do_build = false;
elseif strcmp(varargin{1}, '--rebuild')
  do_clean = true;
  do_build = true;
else
  error('build_mex:badArg', 'Unknown argument: %s', varargin{1});
end

% Define all MEX targets: {source_file, {header_deps...}, mex_args...}
% Header dependencies are checked to trigger rebuilds when headers change.
targets = {
  % Video reading functions
  {'open_h265_video.c', {'h265_frame_cache.h'}, '-lavformat', '-lavcodec', '-lavutil'}
  {'read_h265_frame.c', {'h265_frame_cache.h', 'h265_decode_common.h'}, '-lavformat', '-lavcodec', '-lavutil', '-lswscale'}
  {'read_h265_frames.c', {'h265_decode_common.h'}, '-lavformat', '-lavcodec', '-lavutil', '-lswscale'}
  {'close_h265_video.c', {'h265_frame_cache.h'}, '-lavformat', '-lavcodec', '-lavutil'}
  % H.265 writing functions
  {'open_h265_write.c', {}, '-lavformat', '-lavcodec', '-lavutil', '-lswscale'}
  {'write_h265_frames.c', {}, '-lavformat', '-lavcodec', '-lavutil', '-lswscale'}
  {'close_h265_write.c', {}, '-lavformat', '-lavcodec', '-lavutil', '-lswscale'}
};

mex_ext = mexext;

% Clean step: delete all MEX files
if do_clean
  deleted_count = 0;
  for i = 1:numel(targets)
    src_file = targets{i}{1};
    [~, name, ~] = fileparts(src_file);
    mex_file = [name '.' mex_ext];

    if exist(mex_file, 'file')
      delete(mex_file);
      fprintf('Deleted %s\n', mex_file);
      deleted_count = deleted_count + 1;
    end
  end
  if ~do_build
    if deleted_count == 0
      fprintf('Nothing to clean.\n');
    else
      fprintf('Deleted %d file(s).\n', deleted_count);
    end
  end
end

% Build step: build out-of-date MEX files
if do_build
  built_count = 0;
  for i = 1:numel(targets)
    src_file = targets{i}{1};
    header_deps = targets{i}{2};
    mex_args = targets{i}(3:end);

    [~, name, ~] = fileparts(src_file);
    mex_file = [name '.' mex_ext];

    src_info = dir(src_file);
    mex_info = dir(mex_file);

    if isempty(src_info)
      error('build_mex:missingSource', 'Source file not found: %s', src_file);
    end

    needs_build = isempty(mex_info) || (src_info.datenum > mex_info.datenum);

    % Check if any header dependency is newer than the MEX file
    if ~needs_build && ~isempty(mex_info)
      for j = 1:numel(header_deps)
        hdr_info = dir(header_deps{j});
        if ~isempty(hdr_info) && (hdr_info.datenum > mex_info.datenum)
          needs_build = true;
          break;
        end
      end
    end

    if needs_build
      fprintf('Building %s...\n', name);
      mex(src_file, mex_args{:});
      built_count = built_count + 1;
    end
  end

  if built_count == 0
    fprintf('All MEX files are up to date.\n');
  else
    fprintf('Built %d file(s).\n', built_count);
  end
end

end  % function
