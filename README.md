# v8Unpack, GCC edition
Добавлен проект CodeBlocks 13.12.
Для сборки требуется Boost.

## Fork of v8Unpack project by Denis Demidov (disa_da2@mail.ru)

[Original project HOME](https://www.assembla.com/spaces/V8Unpack/team)

[Original project svn repo](http://svn2.assembla.com/svn/V8Unpack/)

## Note

V8Unpack - a small console program  for rebuild/build configuration files [1C](http://1c.ru) such as *.cf *.epf *.erf
 
## Plaform 

Windows, POSIX

## Build

[![Build status](https://ci.appveyor.com/api/projects/status/48ac3trblfjjkts7/branch/master?svg=true)](https://ci.appveyor.com/project/dmpas/v8unpack/branch/master)

[Прямая ссылка на последнюю успешную сборку win32](https://storage.yandexcloud.net/appveyor/v8unpack/develop/win/v8unpack.exe)

### Ubuntu/Debian

```
curl https://storage.yandexcloud.net/appveyor/v8unpack/develop/ubuntu/v8unpack-3.0.43~bionic.deb --output v8unpack.deb
sudo dpkg -i v8unpack.deb
```

## Version 3.0

- Оптимизирована сборка .cf файла ключ -B[UILD]. В версии 2.0 сборка корневого контейнера происходила в оперативной памяти.
При сборке больших конфигураций это могло приводить к ошибке "segmentation fault". В версии 3.0 сборка корневого контейнера происходит 
динамически с сохранением элементов контейнера непосредственно в файл по мере их создания.


## Использование

```
  -U[NPACK]            in_filename.cf     out_dirname
  -U[NPACK]  -L[IST]   listfile
  -PA[CK]              in_dirname         out_filename.cf
  -PA[CK]    -L[IST]   listfile
  -I[NFLATE]           in_filename.data   out_filename
  -I[NFLATE] -L[IST]   listfile
  -D[EFLATE]           in_filename        filename.data
  -D[EFLATE] -L[IST]   listfile
  -P[ARSE]             in_filename        out_dirname
  -P[ARSE]   -L[IST]   listfile
  -B[UILD] [-N[OPACK]] in_dirname         out_filename
  -B[UILD] [-N[OPACK]] -L[IST] listfile
```
