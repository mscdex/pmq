
Description
===========

A [node.js](http://nodejs.org/) addon for using POSIX message queues.


Requirements
============

* [node.js](http://nodejs.org/) -- v0.8.0 or newer

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

Events
------

* **messages**() - Emitted every time the queue goes from empty to having at least one message.

* **drain**() - Emitted when there is room for at least one message in the queue.

Properties (read-only)
----------------------

* **isFull** - _boolean_ - Convenience property that returns true if `curmsgs` === `maxmsgs`.

* **maxmsgs** - _integer_ - The maximum number of messages in the queue.

* **msgsize** - _integer_ - The maximum size of messages in the queue.

* **curmsgs** - _integer_ - The number of messages currently in the queue.

Methods
-------

* **(constructor)**() - Creates and returns a new PosixMQ instance.

* **open**(<_object_>config) - _(void)_ - Connects to a queue. Valid properties in `config` are:

    * name - _string_ - The name of the queue to open, it **MUST** start with a '/'.

    * create - _boolean_ - Set to `true` to create the queue if it doesn't already exist (default is `false`). The queue will be owned by the user and group of the current process.

    * exclusive - _boolean_ - If creating a queue, set to true if you want to ensure a queue with the given name does not already exist.

    * mode - _mixed_ - If creating a queue, this is the permissions to use. This can be an octal string (e.g. '0777') or an integer.

    * maxmsgs - _integer_ - If creating a queue, this is the maximum number of messages the queue can hold. This value is subject to the system limits in place and defaults to 10.

    * msgsize - _integer_ - If creating a queue, this is the maximum size of each message (in bytes) in the queue. This value is subject to the system limits in place and defaults to 8192 bytes.
    
* **close**() - _(void)_ - Disconnects from the queue.

* **unlink**() - _(void)_ - Removes the queue from the system.

* **push**(< _Buffer_ >data[, < _integer_ >priority]) - _boolean_ - Pushes a message with the contents of `data` onto the queue with the optional `priority` (defaults to 0). `priority` is an integer between 0 and 31 inclusive.

* **shift**(< _Buffer_ >readbuf[, < _boolean_ >returnTuple]) - _mixed_ - Shifts the next message off the queue and stores it in `readbuf`. If `returnTuple` is set to true, an array containing the number of bytes in the shifted message and the message's priority are returned, otherwise just the number of bytes is returned (default). If there was nothing on the queue, false is returned.
