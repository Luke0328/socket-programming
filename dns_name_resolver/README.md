# DNS Name Resolver
Multi-threaded application that resolves domain names to IP addresses. 

The application consists of multiple requester and resolver threads. The requester threads write hostnames to a shared buffer, which are read and mapped to IP addresses by the resolver threads.

Semaphores and mutexes are used to ensure thread-safety.