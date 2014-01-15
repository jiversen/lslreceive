%% test lslrecive max patch
nChan = 10;

%% instantiate the library
disp('Loading library...');
lib = lsl_loadlib();

% make a new stream outlet
disp('Creating a new streaminfo...');
info = lsl_streaminfo(lib,'MAX','AudioControl',nChan,0,'cf_string','JRI_lslreceive');

disp('Opening an outlet...');
outlet = lsl_outlet(info);

%% send data into the outlet, sample by sample
disp('Now transmitting data...');
counter = 0;
while true
    data = repmat({num2str(counter+0.5)},1,nChan);
    outlet.push_sample(data);
    pause(1);
    counter = counter+1;
end