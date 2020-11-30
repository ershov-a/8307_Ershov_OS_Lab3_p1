#include <iostream>
#include <windows.h>
#include <cmath>
#include <iomanip>
#include <atomic>
#include <chrono>

/*
 * Программа компилировалась с использованием MSVC
 * потому что на момент написания только с этим
 * компилятором удалось использовать функцию std::atomic
 * fetch_add для атомарного инкремента глобальной переменной
 * в потоке (fetch_add и fetch_sub для floating-point типов
 * появились только в С++20).
 * */

// Точность - 100000000 по заданию (количество итераций)
#define N 100000000

// Размер блока - 10*номерСтудБилета = 830704*10 = 8307040
#define BLOCK_SIZE 8307040

// Количество блоков: распределяем N итераций по BLOCK_SIZE блокам.
// Если без остатка не делится, то добавляем еще один блок
#define NUMBER_OF_BLOCKS N/BLOCK_SIZE +(N % BLOCK_SIZE ? 1 : 0)


// Массив HANDLE'ов потоков
HANDLE *threadsArray;
// Массив HANDLE'ов событий
HANDLE *eventsArray;

/*
 * std::atomic - атомарные операции в C++ начиная с С++11
 * В С++20 добавлены атомарные операции сложения и вычитания для floating-point типов
 * (source: https://en.cppreference.com/w/cpp/atomic/atomic#Specializations_for_floating-point_types)
 * */
std::atomic<long> nextBlock = 0;
std::atomic<double> pi = 0;

// Функция, которую выполняет поток
DWORD WINAPI calculateIteration(CONST LPVOID threadID) {

    // "Часть" числа Пи, которую считаем в данном потоке
    double threadPi = 0;

    /*
     * Получаем номер потока, который передали
     * в функцию при создании потока.
     * Номер потока также равен номеру
     * первого блока для расчета.
     * */
    int currentBlock = (int) threadID;

    /*
     * Обсчитываем блоки в потоке
     * пока номер текущего блока
     * не превысил заданное количество блоков.
     * */
    while (currentBlock <= NUMBER_OF_BLOCKS) {
        /*
         * При каждом заходе в цикл рассчитывается
         * начальная и конечная граница обсчета.
         * При этом начальная граница - currentBlock * BLOCK_SIZE,
         * а currentBlock изменяется в конце цикла,
         * получая значение nextBlock+1.
         * Т.о. поток обсчитал блок, получил следущий блок, приостановился.
         * */

        /*
         * Начальная границ обсчета.
         * */
        int startIteration = currentBlock * BLOCK_SIZE;

        /*
         * Конечная граница обсчета.
         * */
        int endIteration = (currentBlock + 1) * BLOCK_SIZE;

        /*
         * Если больше считать не нужно,
         * то и цикл ниже запускать не нужно.
         * */
        if (endIteration > N){
            endIteration = N;
        }

        /*
         * Основная часть - расчет "фрагмента" Пи,
         * соответствующего текущему блоку.
         * */
        for (int i = startIteration; i < endIteration; i++){
            threadPi += 4 / (1 + pow((i + 0.5) / N, 2));
        }

        /*
         * Переводим событие, соответствующее потоку,
         * в сигнальное состояние, сигнализируя об окончания расчета
         * очередного блока.
         * */
        SetEvent(eventsArray[(int) threadID]);

        /*
         * Если это был не последний блок, приостанавливаем выполнение потока.
         * */
        if (nextBlock <= NUMBER_OF_BLOCKS) {
            SuspendThread(threadsArray[(int) threadID]);
        }

        /*
         * currentBlock = InterlockedExchangeAdd(&nextBlock, 1)
         *            ^^^ тоже работает (currentBlock объявить как volatile long nextBlock = 0)
         * source: https://docs.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-interlockedexchangeadd
         * InterlockedExchangeAdd - атомарная операция сложения 32битных значений из Win32 API
         * При этом в некотрых источниках написано, что volatile не предназначен
         * стандартом языка для обеспечения атомарности операций в многопоточном программировании
         * и volatile не рекомендуется использовать в таком контексте.
         * */

        /*
         * Инкрементируем глобальную nextBlock и
         * присваем полученное значение currentBlock.
         * Таким образом поток получает следующий блок.
         * */
        currentBlock = nextBlock.fetch_add(1, std::memory_order::memory_order_relaxed);
    }

    /*
     * Когда все блоки обсчитаны,
     * собираем результаты вычислений данного потока
     * в "глоабльное" Пи.
     * */
    pi.fetch_add(threadPi, std::memory_order_relaxed);

    return 0;
}

void calculatePi() {
    /*
     * Получаем количество потоков/
     * */
    int numberOfThreads;
    std::cout << "Enter number of threads" << std::endl;
    std::cin >> numberOfThreads;

    /*
     * Cоздаем массивы HANDLE'ов событий и потоков
     * в соответствии с введенным числом потоков.
     * */
    threadsArray = new HANDLE[numberOfThreads];
    eventsArray = new HANDLE[numberOfThreads];

    /*
     * Создаем потоки в приостановленном состоянии
     * и события в не сигнальном состоянии.
     * При этом в каждый блок передается значение
     * переменной цикла i. С помощью этого каждый
     * поток получает "отправную" точку для начала расчета
     * (первый блок).
     * */
    for (int i = 0; i < numberOfThreads; i++) {
        threadsArray[i] = CreateThread(nullptr, 0, calculateIteration, (LPVOID) i, CREATE_SUSPENDED, nullptr);
        if (!threadsArray[i])
            std::cout << "Could not create thread #" << i << ". Error " << GetLastError() << std::endl;
        eventsArray[i] = CreateEventA(nullptr, true, 0, nullptr);
    }

    /*
     * nextBlock - глобальный счетчик блоков.
     * Так как выше каждый поток получил по блоку,
     * то nextBlock = число потоков.
     * */
    nextBlock = numberOfThreads;

    // Начинаем замерять время выполнения.
    auto start = std::chrono::high_resolution_clock::now();

    // Возобновляем выполнение всех потоков.
    for (int i = 0; i <= numberOfThreads; i++) {
        ResumeThread(threadsArray[i]);
    }

    /*
     * nextBlock атомарно инкрементируется в потоках
     * после завершения обсчета очередного блока.
     * Атомарность позволяет обеспечить безопасный доступ
     * к переменной из нескольких потоков:
     *      1) Так как атомарная операция неделима, вторая атомарная
     *      операция над одним и тем же объектом из другого потока
     *      может получить состояние объекта только до или после
     *      первой атомарной операции.
     *      2) На основе своего аргумента memory_order атомарная
     *      операция устанавливает требования упорядоченности для
     *      видимости влияния других атомарных операций в том же потоке.
     *      Следовательно, она подавляет оптимизации компилятора,
     *      которые нарушают требования к упорядоченности.
     * (source: https://docs.microsoft.com/ru-ru/cpp/standard-library/atomic?view=msvc-160)
     * В этой работе атомарные операции используются для инкремента
     * счетчика подсчета блоков nextBlock и для "сбора" результата вычислений из блоков
     * в переменную pi (обе операции производятся в потоках).
     * */

    // Начинаем считать
    while (nextBlock <= NUMBER_OF_BLOCKS) {
        /*
         * Ждем первого события, которое изменит состояние на сигнальное.
         * Т.е. поток по окончании расчета очередного блока изменит состояния события
         * и приостановится.
         * suspendedThreadIndex получит индекс этого события (и приостановленного потока
         * соответственно) в массиве events.
         * */
        unsigned int suspendedThreadIndex = WaitForMultipleObjects(numberOfThreads, eventsArray, false, INFINITE) - WAIT_OBJECT_0;

        // Сбрасываем состояние этого события в не сигнальное.
        ResetEvent(eventsArray[suspendedThreadIndex]);

        // Возобновляем выполнение потока (он уже получил следующий блок)
        ResumeThread(threadsArray[suspendedThreadIndex]);
    }

    /*
     * Все блоки обсчитаны, нужно собрать результат.
     * */
    for (int i = 0; i < numberOfThreads; i++){
        /*
         * Для этого возобновляем все потоки, чтобы
         * после основного цикла все потоки сложили свои результаты
         * в глобальную переменную pi.
         * */
        ResumeThread(threadsArray[i]);
    }

    // Ждем пока все потоки не завершат своё выполнение.
    WaitForMultipleObjects(numberOfThreads, threadsArray, true, INFINITE);

    // Досчитываем Пи
    pi = pi / N;

    // Заканчиванием замерять время выполнения.
    auto end = std::chrono::high_resolution_clock::now();
    // Подсчитываем затраченное время.
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Выводим результат и затраченное время
    std::cout << "Pi = " << std::setprecision(N) << pi << std::endl
         << "Not all decimal digits are shown due to system limitations" << std::endl
         << "Time elapsed: " << time << " ms"
         << std::endl;

    // Закрываем HANDLE'ы событий и потоков.
    for (int i = 0; i < numberOfThreads; i++) {
        CloseHandle(threadsArray[i]);
        CloseHandle(eventsArray[i]);
    }
}


int main() {
    calculatePi();
    system("pause");
    return 0;
}