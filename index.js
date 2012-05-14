var PosixMQ = require('./build/Release/posixmq');
var EventEmitter = require('events').EventEmitter;

PosixMQ.PosixMQ.prototype.__proto__ = EventEmitter.prototype;

module.exports = PosixMQ.PosixMQ;