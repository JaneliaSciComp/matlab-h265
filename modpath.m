function modpath()
 % Add needed libraries to Matlab path

 % Sort out where we are in the filesytem
 path_to_this_script = mfilename('fullpath') ;
 path_to_this_folder = fileparts(path_to_this_script) ;
 
 % Run the subdir modpath script
 toolbox_folder_path = fullfile(path_to_this_folder, 'toolbox') ;
 toolbox_modpath_script_path = fullfile(toolbox_folder_path, 'modpath.m') ;
 run(toolbox_modpath_script_path) ;  
 
 % Finally, add this folder itself, so we don't have to stay in this folder
 addpath(path_to_this_folder) ;
end
