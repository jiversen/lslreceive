%% test lslrecive max patch
nChan = 10;
doString = true;

%% instantiate the library
disp('Loading library...');
lib = lsl_loadlib();

% make a new stream outlet
disp('Creating a new streaminfo...');
if doString,
  info = lsl_streaminfo(lib,'MAX','AudioControl',nChan,0,'cf_string','');
else
  info = lsl_streaminfo(lib,'floatstream','AudioControl',nChan,0,'cf_float32','');
end

disp('Opening an outlet...');
outlet = lsl_outlet(info);

%% send data into the outlet, sample by sample
disp('Now transmitting data...');
counter = 0;
while true
  if doString,
    data = repmat({num2str(counter+0.5)},1,nChan);
  else
    data = (counter+0.5) * ones(1,nChan);
  end
  outlet.push_sample(data);
  pause(1);
  counter = counter+1;
end