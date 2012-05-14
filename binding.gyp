{
  'targets': [
    {
      'target_name': 'posixmq',
      'sources': [
        'posixmq.cc',
      ],
      'cflags': [ '-O3' ],
      'ldflags': [ '-lrt' ],
    },
  ],
}