function [heading, isDone] = getHeadingFF(curPosLLH_lat, curPosLLH_lon, heading)
    persistent delWayLat_m delWayLon_m waypointInd
    persistent delWayHeading desHeading
    global newWaypoints_lat newWaypoints_lon
    global LATPERM LONPERM delHeadingMax

    if (isempty(delWayLat_m))
        waypointInd = 1;
        delWayLat_m = (newWaypoints_lat(waypointInd+1) - newWaypoints_lat(waypointInd)) / LATPERM;
        delWayLon_m = (newWaypoints_lon(waypointInd+1) - newWaypoints_lon(waypointInd)) / LONPERM;
        delWayMag = sqrt(delWayLat_m*delWayLat_m + delWayLon_m*delWayLon_m);
        delWayLat_m = delWayLat_m / delWayMag;
        delWayLon_m = delWayLon_m / delWayMag;
        delWayHeading = 0;
        desHeading = atan2(delWayLat_m, delWayLon_m)
    end
    
    isDone = false;
    delLat_m = (newWaypoints_lat(waypointInd) - curPosLLH_lat) / LATPERM;
	delLon_m = (newWaypoints_lon(waypointInd) - curPosLLH_lon) / LONPERM;
    delMag = sqrt(delLat_m^2 + delLon_m^2);
% 	setLat_m = delLat_m;
% 	setLon_m = delLon_m;
    
	proj = (delLat_m*delWayLat_m) + (delLon_m*delWayLon_m);
    proj2 = delLat_m*(-delWayLon_m) + delLon_m*(delWayLat_m);
    angleError = max(min(proj2,1.5),-1.5);
%     angleError = sin(acos(proj / delMag))*delMag
    
    if (proj < 0)
        prevHead = atan2(delWayLat_m, delWayLon_m);
        waypointInd = waypointInd + 1;
        if (waypointInd > length(newWaypoints_lat))
            isDone = true;
        elseif (waypointInd == length(newWaypoints_lat))
            delWayLat_m = (newWaypoints_lat(waypointInd) - newWaypoints_lat(waypointInd-1)) / LATPERM;
            delWayLon_m = (newWaypoints_lon(waypointInd) - newWaypoints_lon(waypointInd-1)) / LONPERM;
        else
            delWayLat_m = (newWaypoints_lat(waypointInd+1) - newWaypoints_lat(waypointInd)) / LATPERM;
            delWayLon_m = (newWaypoints_lon(waypointInd+1) - newWaypoints_lon(waypointInd)) / LONPERM;
        end
        delWayMag = sqrt(delWayLat_m*delWayLat_m + delWayLon_m*delWayLon_m);
        delWayLat_m = delWayLat_m / delWayMag;
        delWayLon_m = delWayLon_m / delWayMag;
        newHead = atan2(delWayLat_m, delWayLon_m);
        delWayHeading = (newHead - prevHead) / delWayMag * .1;
    end
    
    if ((delLat_m^2 + delLon_m^2) > 10)
        desHeading = atan2(delLat_m, delLon_m) + delWayHeading;
    else
        desHeading = heading + delWayHeading - angleError*.01;
    end
    delHeading = mod((desHeading - heading)*.85 + pi, 2*pi) - pi;
    if (abs(delHeading) > delHeadingMax)
        delHeading = delHeadingMax * sign(delHeading);
    end
    heading = heading + delHeading;
    
end