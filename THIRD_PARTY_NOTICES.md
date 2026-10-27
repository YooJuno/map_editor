# Third-party components

The Mapwright application source is MIT licensed. Third-party components retain
their own licenses; the application license does not replace them.

* **Qt 6 Core, Gui, Widgets** (and Test for tests), The Qt Company and contributors.
  Linked dynamically. Open-source Qt modules used here are available under LGPL
  v3 / GPL terms, or a commercial Qt license. Qt itself contains additional
  third-party components. See the notices and license files of the exact Qt build
  used for distribution, https://www.qt.io/licensing/open-source-lgpl-obligations
  and https://doc.qt.io/qt-6/licenses-used-in-qt.html.
  Source: https://download.qt.io/archive/qt/ . When distributing bundled Qt,
  include the corresponding Qt and third-party notices and satisfy the applicable
  source/relinking requirements. The CI packaging workflow preserves license files
  supplied by the installed Qt SDK when available, but is not a substitute for
  reviewing the specific SDK's third-party notices before public distribution.
* **yaml-cpp 0.8.0**, Copyright (c) 2008-2015 Jesse Beder.
  MIT License. Source: https://github.com/jbeder/yaml-cpp/tree/0.8.0 .
  A copy of its MIT license is in `licenses/yaml-cpp-LICENSE`.

No browser engine, ROS installation, Python interpreter, telemetry, or network
service is used by the application at runtime. CMake may fetch yaml-cpp at build
time when an installed >= 0.8 version is unavailable.
