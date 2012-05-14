
Description
===========

A [node.js](http://nodejs.org/) addon for using POSIX message queues.


Requirements
============

* [node.js](http://nodejs.org/) -- v0.6.0 or newer

* Linux 2.6.6+ or FreeBSD kernel with POSIX message queue support compiled in (CONFIG_POSIX_MQUEUE, which is enabled by default)

  * See `man mq_overview` for how/where to modify global POSIX message queue resource limits


Install
=======

npm install pmq


Examples
========

* Open an existing queue, read all of its messages, and then remove and close it:
```javascript
    var PosixMQ = require('pmq');
    var readbuf, mq;

    mq = new PosixMQ();
    mq.on('messages', function() {
      var n;
      while ((n = this.shift(readbuf)) !== false) {
        console.log('Received message (' + n + ' bytes): ' + readbuf.toString('utf8', 0, n));
        console.log('Messages left: ' + this.curmsgs);
      }
      this.unlink();
      this.close();
    });
    mq.open({ name: '/pmqtest' });
    readbuf = new Buffer(mq.msgsize);
```
* Create a new queue accessible by all, fill it up, and then close it:
```javascript
    var PosixMQ = require('pmq');
    var mq, writebuf, r;

    mq = new PosixMQ();
    mq.open({
      name: '/pmqtest',
      create: true,
      mode: '0777'
    });
    writebuf = new Buffer(1);
    do {
      writebuf[0] = Math.floor(Math.random() * 93) + 33;
    } while ((r = mq.push(writebuf)) !== false);
    mq.close();
```


API
===

PosixMQ instance events
-----------------------

* **messages**() - Emitted every time the queue goes from empty to having at least one message.

* **drain**() - Emitted when there is room for at least one message in the queue.

PosixMQ instance methods
------------------------

* **(constructor)**() - Creates and returns a new PosixMQ instance.

* **open**(<_object_>config) - _(void)_ - Connects to a queue. Valid properties in _config_ are:

    * <_string_>name - The name of the queue to open, it **MUST** start with a '/'.

    * <_boolean_>create - Set to true to create the queue if it doesn't already exist. The queue will be owned by the user and group of the current process.

    * <_boolean_>exclusive - If creating a queue, set to `true` if you want to ensure a queue with the given name does not already exist.

    * <_mixed_>mode - If creating a queue, this is the permissions to use. This can be an octal string (e.g. '0777') or an integer.

    * <_integer_>maxmsgs - If creating a queue, this is the maximum number of messages the queue can hold. This value is subject to the system limits in place and defaults to `10`.

    * <_integer_>msgsize - If creating a queue, this is the maximum size of each message (in bytes) in the queue. This value is subject to the system limits in place and defaults to `8192` bytes.
    
* **close**() - _(void)_ - Disconnects from the queue.

* **unlink**() - _(void)_ - Removes the queue from the system.

* **push**(<_Buffer_>data[, <_integer_>priority]) - <_boolean_> - Pushes a message with the contents of _data_ onto the queue with the optional _priority_ (defaults to 0). The _priority_ is an integer between 0 and 31 inclusive.

* **shift**(<_Buffer_>readbuf[, <_boolean_>returnTuple]) - _(mixed)_ - Shifts the next message off the queue and stores it in _readbuf_. If _returnTuple_ is set to true, an Array containing the number of bytes in the shifted message and the message's priority are returned, otherwise just the number of bytes is returned (default). If there was nothing on the queue, false is returned.

PosixMQ instance read-only properties
-------------------------------------

* **isFull** - <_boolean_> - Convenience property that returns true if _curmsgs_ === _maxmsgs_.

* **maxmsgs** - <_integer_> - The maximum number of messages in the queue.

* **msgsize** - <_integer_> - The maximum size of messages in the queue.

* **curmsgs** - <_integer_> - The number of messages currently in the queue.
