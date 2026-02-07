function ufmf2avi(infile, outfile, frms, varargin)
% UFMF2AVI Convert a UFMF file to AVI
%
%   ufmf2avi(infile, outfile)
%   ufmf2avi(infile, outfile, frms)
%   ufmf2avi(infile, outfile, frms, 'frame_rate', 30)
%
%   Arguments:
%     frms - number of frames to convert (default: all frames)
%
%   Optional parameters:
%     frame_rate - output frame rate in fps (default: computed from timestamps)

frame_rate = myparse(varargin, 'frame_rate', []);

[readframe, nframes, movie_fid, headerinfo] = get_readframe_fcn(infile);
if movie_fid > 0
  fid_cleanup = onCleanup(@() fclose(movie_fid));
end

if nargin >= 3 && ~isempty(frms)
  nframes = frms;
end

% Determine frame rate
if isempty(frame_rate)
  if isfield(headerinfo, 'timestamps') && nframes > 1
    timestamps = headerinfo.timestamps;
    avg_dt = (timestamps(end) - timestamps(1)) / (nframes - 1);
    frame_rate = 1 / avg_dt;
  else
    error('ufmf2avi:noFrameRate', ...
      'Cannot compute frame rate from single-frame video. Specify ''frame_rate'' manually.');
  end
end

writerObj = VideoWriter(outfile);
writerObj.FrameRate = frame_rate;
writerObj.Quality = 60;
open(writerObj);

for ndx = 1:nframes
  ii = readframe(ndx);
  writeVideo(writerObj, ii);
end

% writerObj closes automatically when it goes out of scope
% fid_cleanup closes movie_fid
