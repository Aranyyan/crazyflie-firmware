%% Script to Estimate Control Effectiveness
% Author: Ewoud Smeur
%
% Use logged data at full speed (512 Hz) of the format:
% 
%  static uint32_t counter;
%  struct FloatRates *rates = stateGetBodyRates_f();
%  struct Int32Vect3 *accel_body = stateGetAccelBody_i();
%  float accelz = ACCEL_FLOAT_OF_BFP(accel_body->z);
%
%  fprintf(file_logger, "%d,%f,%f,%f,%d,%d,%d,%d,%f\n",
%          counter,
%          rates->p,
%          rates->q,
%          rates->r,
%          stabilization_cmd[COMMAND_THRUST],
%          stabilization_cmd[COMMAND_ROLL],
%          stabilization_cmd[COMMAND_PITCH],
%          stabilization_cmd[COMMAND_YAW],
%          accelz
%         );
%  counter++;
%

clc
clear all
close all

filename = 'logDataMel4.csv';

frequency = 500;

%% Read in Data

formatSpec = '%f%f%f%f%f%f%f%f%f';

formatSpecHeader = '%s%s%s%s%s%s%s%s%s';
delimiter = ',';
startRow = 1;
fileID = fopen(filename,'r');
header = textscan(fileID, formatSpecHeader,1, 'Delimiter', delimiter, 'EmptyValue' ,NaN);
dataArray = textscan(fileID, formatSpec, 'Delimiter', delimiter, 'EmptyValue' ,NaN,'HeaderLines' ,startRow, 'ReturnOnError', false);
fclose(fileID);

N = length(dataArray{1, 8});

counter = dataArray{1, 1}(1:N);
gyro(:,1) = dataArray{1, 2}(1:N); %roll
gyro(:,2) = dataArray{1, 3}(1:N); %pitch
gyro(:,3) = dataArray{1, 4}(1:N); %yaw
stab_cmd(:,1) = dataArray{1, 5}(1:N); %thrust
stab_cmd(:,2) = dataArray{1, 6}(1:N); %roll
stab_cmd(:,3) = dataArray{1, 7}(1:N); %pitch
stab_cmd(:,4) = dataArray{1, 8}(1:N); %yaw
accelz = dataArray{1, 9}(1:N); %accel body z

%% Filter signals

% The filter needed to get rid of the noise on the gyro
[b, a] = butter(4,4/(frequency/2));
first_order_dynamics_constant = 0.03149;

% Filter the stabilization command with the actuator dynamics to get an
% estimate of the actuator state
cmd_act_dyn = filter(first_order_dynamics_constant,[1, -(1-first_order_dynamics_constant)], stab_cmd,[],1);

% Filter the gyro and the command with the same filter in order to give
% them the same delay
gyro_filt = filter(b,a,gyro);
cmd_filt = filter(b,a,cmd_act_dyn);
accelz_filt = filter(b,a,accelz);

% Differentiate the gyro to get angular acceleration
angular_accel_filt = [zeros(1,3); diff(gyro_filt,1)]*frequency;
accelzd_filt = [0; diff(accelz_filt,1)]*frequency;

% Differentiate once more to do the estimation
angular_accel_filtd = [zeros(1,3); diff(angular_accel_filt,1)]*frequency;
cmd_filtd = [zeros(1,4); diff(cmd_filt,1)]*frequency;
cmd_filtdd = [zeros(1,4); diff(cmd_filtd,1)]*frequency;

%% Do the fitting with least squares

G_roll = cmd_filtd(:,2)\angular_accel_filtd(:,1);
G_pitch = cmd_filtd(:,3)\angular_accel_filtd(:,2);
G_yaw = [cmd_filtd(:,4) cmd_filtdd(:,4)]\angular_accel_filtd(:,3);
G_specific_force = cmd_filtd(:,1)\accelzd_filt;

%% Plotting
disp('Fill in these values in your airframe file:')
%disp(['G_roll = ' num2str(G_roll) ' G_pitch = ' num2str(G_pitch) ' G1_yaw = ' num2str(G_yaw(1)) ' G2_yaw = ' num2str(G_yaw(2)*1000) ' Specific_force_gain = ' num2str(1/G_specific_force)])
disp(['G_roll = ' num2str(G_roll) ' G_pitch = ' num2str(G_pitch) ' G1_yaw = ' num2str(G_yaw(1)) ' G2_yaw = ' num2str(G_yaw(2)*1000) ])

figure;
plot(angular_accel_filtd(:,1)); hold on
plot(cmd_filtd(:,2)*G_roll)
title('\Delta angular acceleration roll')

figure;
plot(angular_accel_filtd(:,2)); hold on
plot(cmd_filtd(:,3)*G_pitch)
title('\Delta angular acceleration pitch')

figure;
plot(angular_accel_filtd(:,3)); hold on
plot([cmd_filtd(:,4) cmd_filtdd(:,4)]*G_yaw)
title('\Delta angular acceleration yaw')

figure;
plot(accelzd_filt); hold on
plot(cmd_filtd(:,1)*G_specific_force)
title('\Delta z acceleration')

function mPInv = matrixPseudoInvSVD(A)
  % Goal: solve A*x == b for x
  % Set up some matrix A (I used a sparse matrix) -- do yourself
  % Set up the vector b -- do yourself
  % Perform SVD on A
  [U,S,V] = svd(A);
  % A == U*S*V' % Not needed, but you can check it yourself to confirm
  % Calc number of singular values
  s = diag(S); % vector of singular values
  tolerance = max(size(A))*eps(max(s));
  p = sum(s>tolerance);
  % Define spaces
  Up = U(:,1:p);
  %U0 = U(:,p+1:Nx);
  Vp = V(:,1:p);
  %V0 = V(:,p+1:Nx);
  %Sp = spdiags( s(1:p), 0, p, p );
  SpInv = spdiags( 1.0./s(1:p), 0, p, p );
  % Calc AInv such that x = AInv * b
  mPInv = Vp * SpInv * Up'; 
end