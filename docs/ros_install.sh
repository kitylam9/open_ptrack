#!/bin/sh
sudo sh -c 'echo "deb http://packages.ros.org/ros/ubuntu raring main" > /etc/apt/sources.list.d/ros-latest.list'
wget http://packages.ros.org/ros.key -O - | sudo apt-key add -
sudo apt-get update
sudo apt-get upgrade -y
sudo apt-get install ros-hydro-desktop-full -y
apt-cache search ros-hydro
sudo rosdep init
rosdep update
sudo apt-get install python-rosinstall -y
sudo apt-get install ros-hydro-robot-state-publisher -y
sudo apt-get install ros-hydro-openni* -y

