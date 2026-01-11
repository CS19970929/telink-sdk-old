@ECHO OFF
SET DIR=%~dp0
SET WRAPPER=%DIR%\gradle\wrapper\gradle-wrapper.jar
IF NOT EXIST "%WRAPPER%" (
    ECHO 未找到 gradle-wrapper.jar
    EXIT /B 1
)
SET JAVA_EXE=%JAVA_HOME%\bin\java.exe
IF NOT EXIST "%JAVA_EXE%" (
    ECHO 请先设置 JAVA_HOME
    EXIT /B 1
)
"%JAVA_EXE%" -jar "%WRAPPER%" %*
