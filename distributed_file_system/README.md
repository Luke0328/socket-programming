# Distributed File System
Client/server-based application that allows a client to store and retrieve files on multiple servers. 
Supports the following commands:
* ls
* get [filename1] [filename2] ... [filenameN]
* put [filename1] [filename2] ... [filenameN]

Client:
```
# ./dfc <command> [filename] ... [filename]
```
Server:
```
# ./dfs <working_directory> <port_no>
```
The configuration file ~/dfc.conf should contain the list of DFS server addresses and port numbers:
```
server dfs1 127.0.0.1:10001
server dfs2 127.0.0.1:10002
server dfs3 127.0.0.1:10003
server dfs4 127.0.0.1:10004
...
server dfsn 127.0.0.1:1000n # n number of servers
```
