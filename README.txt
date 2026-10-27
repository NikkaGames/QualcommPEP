qcpep: SM8845 Molokai Windows ARM64 PEP

Open QcPep8845.sln with Visual Studio 2022 and WDK 26100.
Build Debug or Release for ARM64, or run build_arm64.cmd Release
from a Visual Studio developer prompt.
Package: ARM64\<configuration>\QcPep8845\qcpep.sys, qcpep.inf, qcpep.cat.
Symbols: ARM64\<configuration>\qcpep.pdb. Intermediates: build\obj.
The WDK handles INF stamping, catalog creation and configured test signing.

Maintenance docs, symbol maps, generators and verification tools:
../qcpep8845_development/README.md

Tracing: WPP and Inflight Trace Recorder in both configurations.
Current image marker: rev=412 api-naming
