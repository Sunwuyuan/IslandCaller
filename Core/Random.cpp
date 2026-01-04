#include "pch.h"
using namespace std;

// 全局变量
vector<string> students;              // 学生名单
bool isInitialized = false;           // 是否已初始化
unordered_set<string> RandomHashSet;  // 用于存储已抽取的学生名单（防止重复）
mutex randomMutex;                    // 线程安全保护：防止多线程并发访问导致的数据竞争

/*
 * 随机选择算法改进说明：
 * 
 * 问题1：随机数生成器种子固定
 * 原实现：使用全局的 random_device 和 mt19937，只在 DLL 加载时初始化一次
 * 改进：每次调用 SimpleRandom 时重新创建 random_device 和 mt19937
 * 效果：确保每次调用都有真正的随机性，不会因为种子固定而产生可预测的序列
 * 
 * 问题2：低效的重试机制
 * 原实现：随机选择后如果发现重复，就递减计数器重新选择
 * 缺点：当已抽取学生接近总数时，可能需要大量重试
 * 改进：使用 Fisher-Yates 洗牌算法，预先构建可用学生列表
 * 效果：时间复杂度从最坏 O(∞) 降低到 O(n)，保证性能稳定
 * 
 * 问题3：缺乏线程安全
 * 原实现：全局变量无保护，多线程访问会导致数据竞争
 * 改进：使用 mutex 和 lock_guard 保护所有共享状态
 * 效果：支持多线程安全调用
 */

EXPORT_DLL int RandomImport(const wchar_t* filenameW)
{
    lock_guard<mutex> lock(randomMutex); // 线程安全保护
    students.clear(); // 清空学生名单
    RandomHashSet.clear(); // 清空已抽取的学生名单
    wstring wstr(filenameW);
    string filename(wstr.begin(), wstr.end());
    filename += ".csv";
    char appDataPath[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath);
    string filePath = string(appDataPath) + "\\IslandCaller\\Profile\\" + filename;
    ifstream file(filePath);
    if (!file) {
        MessageBox(NULL, (L"IslandCaller: Failed to open: " + wstring(filename.begin(), filename.end())).c_str(), L"Error", MB_ICONERROR);
        return -1;
    }
    string name;
    unordered_set<string> ImportHashSet;
    string line;
    getline(file, line); // 不保存第一行标题
    while (getline(file, line))
    {
        stringstream ss(line);
        string token;
        int columnIndex = 0;
        name.clear();

        while (getline(ss, token, ','))
        {
            if (columnIndex == 1)
            {
                if (!token.empty() && token.front() == '"')
                    token.erase(0, 1);
                if (!token.empty() && token.back() == '"')
                    token.pop_back();
                name = token;
                break;
            }
            columnIndex++;
        }

        if (students.size() >= students.max_size()) {
            MessageBox(NULL, L"IslandCaller: Student list size exceeds maximum capacity!", L"Error", MB_ICONERROR);
            file.close();
            return -1;
        }

        name.erase(0, name.find_first_not_of(" \t\n\r"));
        name.erase(name.find_last_not_of(" \t\n\r") + 1);

        if (name.empty()) continue;
        if (ImportHashSet.find(name) != ImportHashSet.end()) continue;

        students.push_back(name);
        ImportHashSet.insert(name);
    }
    file.close();

    // 检查名单是否为空
    if (students.empty()) {
        MessageBox(NULL, L"IslandCaller: Namelist is empty!", L"Error", MB_ICONERROR);
        return -1;
    }

    ImportHashSet.clear(); // 清空导入的哈希集
    isInitialized = true;
    return 0;
}

EXPORT_DLL void ClearHistory()
{
    lock_guard<mutex> lock(randomMutex); // 线程安全保护
    RandomHashSet.clear(); // 清空已抽取的学生名单
}

//点名器函数
EXPORT_DLL BSTR SimpleRandom(const int number)
{
    lock_guard<mutex> lock(randomMutex); // 线程安全保护
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter; // UTF-8 => UTF-16
    if (!isInitialized)
    {
        return SysAllocString(converter.from_bytes("Not Initialized!").c_str());
    }
    string output = "";
    if (number > students.size())
    {
        return SysAllocString(converter.from_bytes("Not enough students!").c_str());// 如果请求的数量超过学生名单，则退出
    }
    
    // 如果已抽取的学生数量等于或超过总学生数，清空历史记录
    if (RandomHashSet.size() >= students.size())
    {
        RandomHashSet.clear();
    }
    
    // 创建可用学生索引列表（未被抽取的学生）
    vector<int> availableIndices;
    availableIndices.reserve(students.size() - RandomHashSet.size());
    for (size_t i = 0; i < students.size(); i++)
    {
        if (RandomHashSet.find(students[i]) == RandomHashSet.end())
        {
            availableIndices.push_back(i);
        }
    }
    
    // 使用高质量随机数生成器进行洗牌
    // 每次调用时使用新的随机种子确保真正的随机性
    random_device rd;
    mt19937 gen(rd());
    
    // 如果需要的学生数量超过可用数量，返回错误
    if (number > availableIndices.size())
    {
        return SysAllocString(converter.from_bytes("Not enough available students!").c_str());
    }
    
    // 使用 Fisher-Yates 洗牌算法随机选择学生
    // 算法原理：从可用学生中逐个随机选择，每次选择后将其与当前位置交换
    // 这样可以保证：1) 每个学生被选中的概率相等  2) 不会重复选择  3) 时间复杂度 O(n)
    for (int i = 0; i < number; i++)
    {
        // 从 [i, availableIndices.size()-1] 范围内随机选择一个位置
        uniform_int_distribution<> dist(i, availableIndices.size() - 1);
        int randomPos = dist(gen);
        
        // 交换当前位置和随机位置的元素
        swap(availableIndices[i], availableIndices[randomPos]);
        
        // 获取被选中的学生
        string selectedStudent = students[availableIndices[i]];
        
        // 添加到输出
        output += selectedStudent;
        
        // 标记该学生已被抽取
        RandomHashSet.insert(selectedStudent);
        
        // 添加分隔符（除了最后一个学生）
        if (i < number - 1)
        {
            output += "  ";
        }
    }
    
    return SysAllocString(converter.from_bytes(output).c_str());
}