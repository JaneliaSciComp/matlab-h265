function modpath()
  % Set up path to use this library.

  % Sort out where we are in the filesytem
  path_to_this_script = mfilename('fullpath') ;
  path_to_this_folder = fileparts(path_to_this_script) ;
  
  % Add this folder to path
  addpath(path_to_this_folder) ;  
end
