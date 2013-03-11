{
  'targets': [
    {
      'target_name': 'posixmq',
      'sources': [
        'src/posixmq.cc',
      ],
      'cflags': [ '-O3' ],
      'ldflags': [ '-lrt' ],
    },
  ],
}