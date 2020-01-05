Disclaimer: Any potential CS 695 students, please don't copy this code.
There are bugs which can result in you wasting more time than you actually need to 
solve this assignment :)

Autoscaling Assignment
Version: 1.0
Author: Diptyaroop Maji, CSE @ IIT-B, MTech-II
Setup: 2 servers on VMs, 1 client in host, 1 monitoring program in host.

Initially, the monitoring program is executed. It boots up server1 (domain schema
XML already existing) and monitors it's CPU utilization. Now, client is started, 
which communicates with server (CREATE, GET, UPDATE, DELETE K-V). If server1 CPU 
utlization exceeds a certain threshold (kept at 80%) for a considerable amount of
time, the monitoring program boots up server2 and notifies the client that server1 
is overloaded.

On receiving the notification, the client now acts as a loda balancer, i.e.,
disconnects half of its threads from server1, and connects them to the server2,
so that load on server1 is decreased (naturally, server2 CPU utilization incraeses).

Note: Both servers have the server application running in the background right
from when they boot up (this is done by modifying the rc.local file).

Important links:
https://vpsfix.com/community/server-administration/no-etc-rc-local-file-on-ubuntu-18-04-heres-what-to-do/
https://stackoverflow.com/questions/40468370/what-does-cpu-time-represent-exactly-in-libvirt
https://libvirt.org/html/index.html
https://libvirt.org/api.html