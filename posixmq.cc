#include <node.h>
#include <node_buffer.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <mqueue.h>

#if defined(__linux__)
#  define MQDES_TO_FD(mqdes) (int)(mqdes)
#elif defined(__FreeBSD__)
#  define MQDES_TO_FD(mqdes) __mq_oshandle(mqdes)
#endif

using namespace node;
using namespace v8;

static Persistent<FunctionTemplate> constructor;
static Persistent<String> emit_symbol;
static Persistent<String> read_symbol;
static Persistent<String> write_symbol;
static Persistent<Value> read_emit_argv[1];
static Persistent<Value> write_emit_argv[1];
static const mqd_t MQDES_INVALID = (mqd_t)-1;

class PosixMQ : public ObjectWrap {
  public:
    mqd_t mqdes;
    struct mq_attr mqattrs;
    ev_io mqpollhandle;
    char* mqname;
    Persistent<Function> Emit;
    bool canread;
    bool canwrite;

    PosixMQ() {
      mqdes = MQDES_INVALID;
      mqname = NULL;
      canread = false;
      canwrite = false;
    }

    ~PosixMQ() {
      if (mqdes != MQDES_INVALID) {
        ev_io_stop(EV_DEFAULT_ &mqpollhandle);
        mq_close(mqdes);
        mqdes = MQDES_INVALID;
      }
      if (mqname) {
        free(mqname);
        mqname = NULL;
      }
      Emit.Dispose();
      Emit.Clear();
    }

    static Handle<Value> New(const Arguments& args) {
      HandleScope scope;

      if (!args.IsConstructCall()) {
        return ThrowException(Exception::TypeError(
            String::New("Use `new` to create instances of this object.")));
      }

      PosixMQ* obj = new PosixMQ();
      obj->Wrap(args.This());

      return args.This();
    }

    static Handle<Value> Open(const Arguments& args) {
      HandleScope scope;
      PosixMQ* obj = ObjectWrap::Unwrap<PosixMQ>(args.This());
      bool doCreate = false;
      int flags = O_RDWR | O_NONBLOCK;
      mode_t mode;
      const char* name;

      if (args.Length() != 1) {
        return ThrowException(Exception::TypeError(
            String::New("Expecting 1 argument")));
      }
      if (!args[0]->IsObject()) {
        return ThrowException(Exception::TypeError(
            String::New("Argument must be an object")));
      }

      Local<Object> config = args[0]->ToObject();
      Local<Value> val;

      if (!(val = config->Get(String::New("create")))->IsUndefined()) {
        if (!val->IsBoolean()) {
          return ThrowException(Exception::TypeError(
              String::New("'create' property must be a boolean")));
        }
        doCreate = val->BooleanValue();
      }

      val = config->Get(String::New("name"));
      if (!val->IsString()) {
        return ThrowException(Exception::TypeError(
            String::New("'name' property must be a string")));
      }
      String::AsciiValue namestr(val->ToString());
      name = (const char*)(*namestr);

      val = config->Get(String::New("mode"));
      if (doCreate) {
        if (val->IsUint32())
          mode = (mode_t)val->Uint32Value();
        else if (val->IsString()) {
          String::AsciiValue modestr(val->ToString());
          mode = (mode_t)strtoul((const char*)(*modestr), NULL, 8);
        } else {
          return ThrowException(Exception::TypeError(
              String::New("'mode' property must be a string or integer")));
        }
        flags |= O_CREAT;

        val = config->Get(String::New("exclusive"));
        if (val->IsBoolean() && val->BooleanValue() == true)
          flags |= O_EXCL;

        val = config->Get(String::New("maxmsgs"));
        if (val->IsUint32())
          obj->mqattrs.mq_maxmsg = val->Uint32Value();
        else
          obj->mqattrs.mq_maxmsg = 10;
        val = config->Get(String::New("msgsize"));
        if (val->IsUint32())
          obj->mqattrs.mq_msgsize = val->Uint32Value();
        else
          obj->mqattrs.mq_msgsize = 8192;
      }

      if (obj->mqdes != MQDES_INVALID) {
        ev_io_stop(EV_DEFAULT_ &(obj->mqpollhandle));
        mq_close(obj->mqdes);
        obj->mqdes = MQDES_INVALID;
      }

      if (doCreate)
        obj->mqdes = mq_open(name, flags, mode, &(obj->mqattrs));
      else
        obj->mqdes = mq_open(name, flags);

      if (obj->mqdes == MQDES_INVALID ||
          mq_getattr(obj->mqdes, &(obj->mqattrs)) == -1) {
        return ThrowException(Exception::Error(
            String::New(uv_strerror(uv_last_error(uv_default_loop())))));
      }

      if (obj->mqname) {
        free(obj->mqname);
        obj->mqname = NULL;
      } else {
        obj->Emit = Persistent<Function>::New(Local<Function>::Cast(
                                               obj->handle_->Get(emit_symbol)));
      }

      obj->mqname = strdup(name);

      obj->canread = !(obj->mqattrs.mq_curmsgs > 0);
      obj->canwrite = !(obj->mqattrs.mq_curmsgs < obj->mqattrs.mq_maxmsg);

      ev_init(&(obj->mqpollhandle), poll_cb);
      obj->mqpollhandle.data = obj;
      ev_io_set(&(obj->mqpollhandle), obj->mqdes, EV_READ | EV_WRITE);
      ev_io_start(EV_DEFAULT_ &(obj->mqpollhandle));

      return Undefined();
    }

    static void poll_cb(EV_P_ ev_io *handle, int revents) {
      HandleScope scope;

      if (revents & EV_ERROR)
        return;

      PosixMQ* obj = (PosixMQ*)handle->data;

      mq_getattr(obj->mqdes, &(obj->mqattrs));

      if ((revents & EV_READ) && !obj->canread) {
        obj->canread = true;

        TryCatch try_catch;
        obj->Emit->Call(obj->handle_, 1, read_emit_argv);
        if (try_catch.HasCaught())
          FatalException(try_catch);
      } else if (!(revents & EV_READ))
        obj->canread = false;

      if ((revents & EV_WRITE) && !obj->canwrite) {
        obj->canwrite = true;
        TryCatch try_catch;
        obj->Emit->Call(obj->handle_, 1, write_emit_argv);
        if (try_catch.HasCaught())
          FatalException(try_catch);
      } else if (!(revents & EV_WRITE))
        obj->canwrite = false;
    }

    static Handle<Value> Close(const Arguments& args) {
      HandleScope scope;
      PosixMQ* obj = ObjectWrap::Unwrap<PosixMQ>(args.This());

      if (obj->mqdes == MQDES_INVALID) {
        return ThrowException(Exception::Error(
            String::New("Queue already closed")));
      }

      ev_io_stop(EV_DEFAULT_ &(obj->mqpollhandle));

      if (mq_close(obj->mqdes) == -1) {
        return ThrowException(Exception::Error(
            String::New(uv_strerror(uv_last_error(uv_default_loop())))));
      }

      obj->mqdes = MQDES_INVALID;

      return Undefined();
    }

    static Handle<Value> Unlink(const Arguments& args) {
      HandleScope scope;
      PosixMQ* obj = ObjectWrap::Unwrap<PosixMQ>(args.This());

      if (!obj->mqname) {
        return ThrowException(Exception::Error(
            String::New("Nothing to unlink")));
      }

      if (mq_unlink((const char*)obj->mqname) == -1) {
        return ThrowException(Exception::Error(
            String::New(uv_strerror(uv_last_error(uv_default_loop())))));
      }

      if (obj->mqname) {
        free(obj->mqname);
        obj->mqname = NULL;
      }

      return Undefined();
    }

    static Handle<Value> Send(const Arguments& args) {
      HandleScope scope;
      PosixMQ* obj = ObjectWrap::Unwrap<PosixMQ>(args.This());
      uint32_t priority = 0;
      bool ret = true;

      if (args.Length() < 1) {
        return ThrowException(Exception::TypeError(
            String::New("Expected at least 1 argument")));
      } else if (!Buffer::HasInstance(args[0])) {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be a Buffer")));
      } else if (args.Length() >= 2) {
        if (args[1]->IsUint32() && args[1]->Uint32Value() < 32)
          priority = args[1]->Uint32Value();
        else {
          return ThrowException(Exception::TypeError(
              String::New("Second argument must be an integer 0 <= n < 32")));
        }
      }

      if (mq_send(obj->mqdes, Buffer::Data(args[0]->ToObject()),
                  Buffer::Length(args[0]->ToObject()), priority) == -1) {
        if (errno != EAGAIN) {
          return ThrowException(Exception::Error(
              String::New(uv_strerror(uv_last_error(uv_default_loop())))));
        }
        ret = false;
      }

      mq_getattr(obj->mqdes, &(obj->mqattrs));

      return scope.Close(Boolean::New(ret));
    }

    static Handle<Value> Receive(const Arguments& args) {
      HandleScope scope;
      PosixMQ* obj = ObjectWrap::Unwrap<PosixMQ>(args.This());
      ssize_t nBytes;
      uint32_t priority;
      bool retTuple = false;
      Local<Value> ret;

      if (args.Length() < 1) {
        return ThrowException(Exception::TypeError(
            String::New("Expected at least 1 argument")));
      } else if (!Buffer::HasInstance(args[0])) {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be a Buffer")));
      } else if (args.Length() > 1)
        retTuple = args[1]->BooleanValue();

      Local<Object> buf = args[0]->ToObject();
      if ((nBytes = mq_receive(obj->mqdes, Buffer::Data(buf),
                               Buffer::Length(buf), &priority)) == -1) {
        if (errno != EAGAIN) {
          return ThrowException(Exception::Error(
              String::New(uv_strerror(uv_last_error(uv_default_loop())))));
        }
        ret = Local<Value>::New(Boolean::New(false));
      } else if (!retTuple)
        ret = Integer::New(nBytes);
      else {
        Local<Array> tuple = Array::New(2);
        tuple->Set(0, Integer::New(nBytes));
        tuple->Set(1, Integer::New(priority));
        ret = tuple;
      }

      mq_getattr(obj->mqdes, &(obj->mqattrs));

      return scope.Close(ret);
    }

    static Handle<Value> MsgsizeGetter (Local<String> property, const AccessorInfo& info) {
      HandleScope scope;
      PosixMQ* obj = ObjectWrap::Unwrap<PosixMQ>(info.This());

      mq_getattr(obj->mqdes, &(obj->mqattrs));

      return scope.Close(Integer::New(obj->mqattrs.mq_msgsize));
    }

    static Handle<Value> MaxmsgsGetter (Local<String> property, const AccessorInfo& info) {
      HandleScope scope;
      PosixMQ* obj = ObjectWrap::Unwrap<PosixMQ>(info.This());

      mq_getattr(obj->mqdes, &(obj->mqattrs));

      return scope.Close(Integer::New(obj->mqattrs.mq_maxmsg));
    }

    static Handle<Value> CurmsgsGetter (Local<String> property, const AccessorInfo& info) {
      HandleScope scope;
      PosixMQ* obj = ObjectWrap::Unwrap<PosixMQ>(info.This());

      mq_getattr(obj->mqdes, &(obj->mqattrs));

      return scope.Close(Integer::New(obj->mqattrs.mq_curmsgs));
    }

    static Handle<Value> IsfullGetter (Local<String> property, const AccessorInfo& info) {
      HandleScope scope;
      PosixMQ* obj = ObjectWrap::Unwrap<PosixMQ>(info.This());

      mq_getattr(obj->mqdes, &(obj->mqattrs));

      return scope.Close(Boolean::New(obj->mqattrs.mq_curmsgs == obj->mqattrs.mq_maxmsg));
    }

    static void Initialize(Handle<Object> target) {
      HandleScope scope;

      Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
      Local<String> name = String::NewSymbol("PosixMQ");

      constructor = Persistent<FunctionTemplate>::New(tpl);
      constructor->InstanceTemplate()->SetInternalFieldCount(1);
      constructor->SetClassName(name);

      NODE_SET_PROTOTYPE_METHOD(constructor, "open", Open);
      NODE_SET_PROTOTYPE_METHOD(constructor, "close", Close);
      NODE_SET_PROTOTYPE_METHOD(constructor, "push", Send);
      NODE_SET_PROTOTYPE_METHOD(constructor, "shift", Receive);
      NODE_SET_PROTOTYPE_METHOD(constructor, "unlink", Unlink);

      constructor->PrototypeTemplate()->SetAccessor(String::New("msgsize"),
                                                    MsgsizeGetter);
      constructor->PrototypeTemplate()->SetAccessor(String::New("maxmsgs"),
                                                    MaxmsgsGetter);
      constructor->PrototypeTemplate()->SetAccessor(String::New("curmsgs"),
                                                    CurmsgsGetter);
      constructor->PrototypeTemplate()->SetAccessor(String::New("isFull"),
                                                    IsfullGetter);
      emit_symbol = NODE_PSYMBOL("emit");
      read_symbol = NODE_PSYMBOL("messages");
      write_symbol = NODE_PSYMBOL("drain");
      read_emit_argv[0] = read_symbol;
      write_emit_argv[0] = write_symbol;
      target->Set(name, constructor->GetFunction());
    }
};

extern "C" {
  void init(Handle<Object> target) {
    HandleScope scope;
    PosixMQ::Initialize(target);
  }

  NODE_MODULE(posixmq, init);
}
