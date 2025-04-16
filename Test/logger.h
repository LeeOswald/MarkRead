#pragma once
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <fcntl.h>
#include <chrono>
#include <strsafe.h>
#include <stdarg.h> // va_list, va_start, va_arg, va_end

// linux #include <unistd.h>

namespace HELP {

// Example:            plogger_->log( proc_name_, actor_name_, msg, level);

/// Перечисление возможных уровней логирования.
enum ELog {LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_DEBUG, LOG_CRITICAL_ERROR, LOG_OBJECTS_DANGER, LOG_OBJECTS_WARNING, PROFILER, PROFILER_START, PROFILER_STOP};
enum EMode { MODE_COUT, MODE_WRITE_TO_FILE };

// Получение текущей директории
inline std::wstring GetCurDir() {
    TCHAR buffer[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, buffer, MAX_PATH);

    return std::wstring(buffer).substr(0, std::wstring(buffer).find_last_of(L"\\"));
}

/*
*@brief Функция, формирующая временную метку - возвращает текущее время в формате год - месяц - день_час!минута!секунда!наносекунда в виде строки
*
*@return std::string строка со временем
*/
inline std::string make_time_stamp(std::string format = "%Y_%m_%d__%H_%M_%S_") {
    static unsigned long long old_time_stamp = 0;

    char time_str[200]; // Строка со временем для форматирования. 
    std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now(); // Старт времени. 
    std::time_t now_c = std::chrono::system_clock::to_time_t(now); // Преобразование времени. 
    auto epoch = now.time_since_epoch();
    auto us = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count(); // Получение точности времени до наносекунды. 

    struct tm timeinfo;
    localtime_s(&timeinfo, &now_c);
    std::strftime(time_str, 200, format.c_str(), &timeinfo);  // Приведение строки со временем к нужному формату. 
       
    std::string sAdd;

    if (old_time_stamp > 0)
        sAdd += std::to_string(us % 1000) + std::string("D") + std::to_string((int)(us - old_time_stamp)); // Добавление разницы между кадрами в миллисекудах
    else
        sAdd += std::to_string(us % 1000) + "D00";

    old_time_stamp = us;

    return (time_str + sAdd);
}

// Возвращает разницу во времени между метками записанную во времянной метке функцией make_time_stamp после символа 'D'
// Обрабатывает не более 5ти позиций по умолчанию. Считает приоритетной четвертую и пятую, учитывет последнюю успешную позицию при вызове
inline long get_delta(const std::string &sFileName, int maxPos=5) {
    static int lastPos = 0;
    int curPos = 0;
    int l = sFileName.length();

    if (sFileName[l-4] != '.')
        return 0; // нет разширения файла, без него не работаем

    curPos =  l-1;
    std::string sRes = "";
    while ((curPos >0) && (sFileName[curPos--] != 'D'))
        ;

    try {
        if (curPos) {
            std::string s = sFileName.substr(curPos+2, l - curPos-6);
            return std::atol(s.c_str());
        }
    }
    catch (...) { return 0; }

    return 0;
}

/// Class Logger выводит логи в файл log.txt и отладочные сообщения в файл debug_log.txt.
/**
 Внимание! Создается статический, глобальный экземпляр класса "log". 
 Каждый сервис использует метод writeLog(int log_level, std::string const &message) для записи лога в лог-файл log.txt, указывая тип сообщения:  <br>
 LOG_DEBUG - сообщения отладки  <br>
 LOG_INFO - информационное сообщение,  <br>
 LOG_WARNING - предупреждение,  <br>
 LOG_ERROR - сообщение об ошибке,  <br>
 LOG_CRITICAL_ERROR - сообщение о критической ошибке.   <br>
 При отладке может быть использован уровень LOG_DEBUG для записи отладочных сообщений в лог-файл debug_log.txt  <br>
*/
class Logger {
public:
    bool is_write_debug_ = false;
    bool is_show_info_ = true;
    bool is_write_profile_ = false;
    void set_format(std::string format) { format_=format; }
    bool m_bInit=false;
    void set_mode(EMode mode) { mode_ = mode; }
private:
    std::string log_file_name_; ///< Название лог-файла.
    HANDLE log_fd_; ///< Дескриптор лог-файла.
    EMode mode_;
    std::string format_ = "%Y_%m_%d__%H_%M_%S_"; ///< Формат даты времени
public:
    Logger() { 
        Init(L"log.txt", MODE_WRITE_TO_FILE); 
     }
    Logger(const std::wstring &sFileName, EMode mode = MODE_WRITE_TO_FILE) : format_("%d.%m.%Y %H:%M:%S:") // pvd format_("%Y_%m_%d__%H_%M_%S_")///< Констуктор: открывает лог-файл. Mode - режим работы, экран, файл, оба
    {
        Init(sFileName, mode);
    }

  void Init(const std::wstring &sFileName = L"log.txt", EMode mode = MODE_WRITE_TO_FILE) {

      log_fd_ = INVALID_HANDLE_VALUE;
      mode_ = mode;
      std::wstring sFN = GetCurDir() + L"\\" + sFileName;
      // Создание файла с указанным именем.
      log_fd_ = CreateFile(sFN.c_str(), GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH , NULL);
      if (log_fd_ == INVALID_HANDLE_VALUE)
          std::cout << "LOG_ERROR: Coudn't create or open logfile: " << std::endl;

      m_bInit = true;
  }

  void Close()
  {
      if ((log_fd_ == INVALID_HANDLE_VALUE) || log_fd_== 0)
          return;

      if (!CloseHandle(log_fd_))
          std::cout << "Error 002: Coudn't close logfile " << log_file_name_ << std::endl;

  }

  ~Logger() { ///< Дестуктор: закрывает лог-файл.
      Close();
  }


  DWORD wb = 0;

  // Не делает переводов строк, не добавляет доп информацию, не выводит на экран, только пишет на диск, если включен режим записи debug на диск
  void log_( std::string const &msg) {
      WriteFile(log_fd_, msg.c_str(), msg.length(), &wb, 0 );
  }
/**
 * @brief В зависимости от полученного уровня логирования записывает сообщение в лог-файл.
 * 
 * @param log_level уровень логирования
 * @param message сообщение для записи в лог
 */
  void log( std::string const & proc_name, std::string const & actor_name, std::string const &message, ELog log_level=HELP::LOG_INFO) {
      // Переменные для записи в файл.
      DWORD wb = 0;
      auto overlapped = OVERLAPPED{ NULL, NULL, 0xFFFFFFFF, 0xFFFFFFFF };
      std::string msg = proc_name+":"+actor_name + ":";
      switch (log_level) {
        case LOG_INFO:
          msg = "\nInfo: " + msg + make_time_stamp() + ":" + message;
          break;
        case LOG_WARNING:
          msg = "\nWarning: " + msg + make_time_stamp() + ": " + message;
          break;
        case LOG_ERROR:
          msg = "\nError: "  + msg + make_time_stamp() + ": " + message;
          break;
        case LOG_CRITICAL_ERROR:
          msg = "\nCritical Error: " + msg + make_time_stamp() + ": " + message;
          WriteFile(log_fd_, msg.c_str(), msg.length(), &wb, &overlapped); std::cout << msg;
          exit(1);
        case LOG_OBJECTS_DANGER:
        case LOG_OBJECTS_WARNING:
          msg =  "\nLOG_OBJECTS:" + make_time_stamp() + " " + message;
          break;
        case LOG_DEBUG:
          msg =  "\nLOG_DEBUG:" + make_time_stamp() + ": " + message;
          break;
        case PROFILER_START:
            msg =  "\nPROFILER_START:" + make_time_stamp() + ": " + message;
            if (!is_write_profile_)
                msg="";
          break;
        case PROFILER_STOP:
          msg =  "\nPROFILER_STOP:" + make_time_stamp() + ": " + message;
            if (!is_write_profile_)
                msg="";
          break;
        case PROFILER:
          msg =  "\nPROFILER:" + make_time_stamp() + ": " + message;
            if (!is_write_profile_)
                msg="";
          break;
        default:
	    msg="\nError log level "+ make_time_stamp() + ": " + message;
      }
 
      if (is_write_debug_) {
          if ((msg!="") && (mode_ == MODE_WRITE_TO_FILE))
              WriteFile(log_fd_, msg.c_str(), msg.length(), &wb, &overlapped);
      } 

      if (wb == -1) ;
      
      std::cout << msg;
   }
};

// Функция - эквивалент std::format в С++ 20. Использовать если не C++20
inline std::string string_sprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    size_t sz = vsnprintf(nullptr, 0, format, args);
    size_t bufsize = sz + 1;
    char* buf = (char*)malloc(bufsize);
    if (buf != nullptr)
        vsnprintf(buf, bufsize, format, args);
    va_end(args);
    std::string str = buf;

    if (buf != nullptr)
        delete(buf);

    return str;
}

/*
Описание:

   Расшифровывает код ошибки GetLastError содержательным текстом

Аргументы:

   Code - Код ошибки для трансляции

*/
inline const std::string &TranslateError(_In_ DWORD Code) {

    WCHAR buffer[MAX_PATH] = { 0 };
    DWORD count;
    HMODULE module = NULL;
    HRESULT status;

    count = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        Code,
        0,
        buffer,
        sizeof(buffer) / sizeof(WCHAR),
        NULL);


    if (count == 0) {

        count = GetSystemDirectory(buffer,
            sizeof(buffer) / sizeof(WCHAR));

        if (count == 0 || count > sizeof(buffer) / sizeof(WCHAR)) {

            //
            //  In practice we expect buffer to be large enough to hold the 
            //  system directory path. 
            //

            return "    Could not translate error: " + std::to_string(Code);
        }


        status = StringCchCat(buffer,
            sizeof(buffer) / sizeof(WCHAR),
            L"\\fltlib.dll");

        if (status != S_OK) {

            return "    Could not translate error: %d\n" + std::to_string(Code);
        }

        module = LoadLibraryExW(buffer, NULL, LOAD_LIBRARY_AS_DATAFILE);

        //
        //  Translate the Win32 error code into a useful message.
        //

        count = FormatMessage(FORMAT_MESSAGE_FROM_HMODULE,
            module,
            Code,
            0,
            buffer,
            sizeof(buffer) / sizeof(WCHAR),
            NULL);

        if (module != NULL) {

            FreeLibrary(module);
        }

        //
        //  If we still couldn't resolve the message, generate a string
        //

        if (count == 0) {
            return "    Could not translate error: %d\n" + std::to_string(Code);
        }
    }

    //
    //  Display the translated error.
    //

    return string_sprintf("    %ws\n", buffer);
}


// Создание экземпляра класса Logger.
extern Logger log;

//!!! Необходимо объявить в модуле HELP::Logger HELP::log; 
}
