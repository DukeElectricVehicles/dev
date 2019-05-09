%%  Gerry Chen
%   GPStestDataAnalyze.m

clear;

figure(1);clf;
img = imread('satelliteImage_1.png');
imagesc(...
    'YData',[36.0024697,36.0013262],'XData',[-78.9466732,-78.9452114],...
    'CData',img); hold on;

% load selfDrive_19-04-26_03_grosshallEarlyFail
% plot(curPosLLH_lon, curPosLLH_lat, 'k-', 'DisplayName','manual control'); hold on;

load selfDrive_19-04-27_13_grosshall_4
% load manualDrive_19-04-27_04_grosshall
indsToPlot = (curPosLLH_lon~=0);

velocity = [velE_mmpers/1e3,velN_mmpers/1e3];

plot(waypoint_lon, waypoint_lat, 'g^', 'DisplayName','path waypoints','MarkerSize',4);
% scatter(curPosLLH_lon(indsToPlot), curPosLLH_lat(indsToPlot), 1, isAutonomous(indsToPlot),...
%     '.', 'DisplayName','path following');
    zdum = zeros(size(indsToPlot));
    colormap autumn
    surface([curPosLLH_lon(indsToPlot),curPosLLH_lon(indsToPlot)],...
            [curPosLLH_lat(indsToPlot),curPosLLH_lat(indsToPlot)],...
            [zdum,zdum],...
            [isAutonomous(indsToPlot),isAutonomous(indsToPlot)],...
            'facecol','no',...
            'edgecol','interp',...
            'linew',1,...
            'DisplayName', 'Vehicle Path');
indsToPlot = find(indsToPlot);
indsToPlot = indsToPlot(1:5:end);
% quiver(curPosLLH_lon(indsToPlot),curPosLLH_lat(indsToPlot),...
%        cos(desHeading(indsToPlot)), sin(desHeading(indsToPlot)),'r-');
%        velE_mmpers(indsToPlot),velN_mmpers(indsToPlot),'r-');
legend show
grid on;
xlim([-78.9466732,-78.9452114]);
ylim([36.0013262,36.0024697]);
daspect([1,cosd(36),1]);
title('GCS Algorithm - Test 4');
xlabel('Latitude (deg)'); ylabel('Longitude (deg)');

%% animation
figure(2);clf;
[odoUni, indsUni] = unique(odo);
imagesc(...
    'YData',[36.0024697,36.0013262],'XData',[-78.9466732,-78.9452114],...
    'CData',img); hold on;
for i = 1:length(indsUni)
    fprintf('%2d\t%2.2f\n',i,odo(indsUni(i)));
    plot(curPosLLH_lon(indsUni(i)), curPosLLH_lat(indsUni(i)),'y.');
    plot(waypoint_lon(indsUni(i)), waypoint_lat(indsUni(i)),'g^');
    drawnow()
end