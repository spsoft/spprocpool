The preforked server architecture

# Introduction #

The preforked server architecture


# Details #

http://lh3.google.com/stephen.nil/R387OKgDxqI/AAAAAAAAAEE/qwdpTrNkYQo/%E7%BB%98%E5%9B%BE3.jpg?imgmax=512

```
MasterServer(ProcessPool)
              |
               `-- ProcessManager
                 |
                 |-- ChildServer1
                 |-- ......
                 `-- ChildServerN
```

  1. MasterServer fork to create ProcessManager, and ProcessPool is a stub of ProcessManager
  1. These a ManagerPipe between ProcessPool and ProcessManager
  1. When MasterServer need more ChildServer, it ask ProcessManager to create ChildServer via ProcessPool, ProcessManager is the only place to fork ChildServer
  1. These a ChildPipe between MasterServer and ChildServer