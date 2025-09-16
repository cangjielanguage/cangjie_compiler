// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <iostream>
#include <string>

#include "gtest/gtest.h"

#include "cangjie/Lex/Lexer.h"

using namespace Cangjie;

class TotalTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        lexer = std::make_unique<Lexer>(code, diag, sm);
    }

    std::string code =
        R"(
main(args: Array<String>) {
//from arithmeticOperators.char
    let a = 2 + 3  //add: 5
    let b = 3 - 1  //sub: 2
    let c = 3 * 4  //multi: 12
    let d = 6.6 / 1.1  //division: 6
    let e = 7 % 3  //mod: 1
    let f = 2**3  // power: 8: 8
    var result = 5+10-3*4**2/3%5  // 5+10-3*(4**2)/3%5=5+10-(3*16)/3%5=5+10-(48/3)%5=5+10-(16%5)=5+10-1=15-1=14

//from array.char
	let arrayZero[2]: Int //define arrayZero whose size is 2
    arrayZero[0] = 10 //initialization of arrayZero
    arrayZero[1] = 20
    let arrayOne[2]: Int = [10, 20] //define arrayOne whose size is 2 and initialize it
    var arrayTwo[]: Int = [10, 20, 30] //define arrayTwo whose size is 3 and initialize it
	true ? arrayOne : arrayTwo  // return arrayOne
    let arrayThree[2][3]: Int //define two-dimensional arrayThree whose size is 2*3
    let arrayFour[2][3]: Int = [10,20,30;40,50,60]//define two-dimensional arrayFour whose size is 2*3 and initialize it
    var arrayZeroSize = arrayZero.size // arrayZeroSize=2
    var arrayFourSize = arrayFour.size // arrayZeroSize = 6

//from bigNumbers.char
	let bigIntNum1: BigInteger = 100000000000000000000
    let bigIntNum2: BigInteger = 200000000000000000000
    var bigIntNum3: BigInteger
    bigIntNum3 = bigIntNum1 + bigIntNum2
    bigIntNum3 = bigIntNum1 - bigIntNum2
    bigIntNum3 = bigIntNum1 * bigIntNum2
    bigIntNum3 = bigIntNum1 / bigIntNum2

    let bigFloatNum1: BigFloat = 1.1E500
    let bigFloatNum2: BigFloat = 1.2E500
    var bigFloatNum3: BigFloat
    bigFloatNum3 = bigFloatNum1 + bigFloatNum2
    bigFloatNum3 = bigFloatNum1 - bigFloatNum2
    bigFloatNum3 = bigFloatNum1 * bigFloatNum2
    bigFloatNum3 = bigFloatNum1 / bigFloatNum2

//from bitwiseLogicalOperators.char
	~10  //bitwise logical NOT operator: 255-10=245
    10 << 1 //left shift operator: 10*2=20
    10 >> 1 //left shift operator: 10/2=5
    10 & 15  //bitwise logical AND operator: 15
    10 ^ 15  //bitwise logical XOR operator: 5
    10 | 15  //bitwise logical OR operator: 15

//from collection.char
    let mListOne: List<Int> = {3} //define read-only List mListOne
    var mListTwo: List<Int> = {4, 5} //define writable List mListTwo
    var mListThree = mListOne + mListTwo      //mListThree={1,2,3,4,5}
    var mListThreeElement = mListThree.get(0)      //mListThreeElement=3
    mListThree.set(2,6)      //mListThree={3,4,6}
    mListThree.add(2,5)      //mListThree={3,4,5,6}
    mListThree.remove(0)      //mListThree={4,5,6}
    mListThree.reverse(0)      //mListThree={6,5,4}
    mListThree.sortInc(0)      //mListThree={4,5,6}
    mListThree.sortDec(0)      //mListThree={6,5,4}
    var mListThreeLength = mListThree==mListTwo ? mListThree.length:mListTwo.length   //mListThreeLength=3
    mListThree.contains(4)   //return true
    mListThree.isEmpty()   //return false
    for item in mListThree {        //visit all elements in mListThree
        print(item)                           //output: 6 5 4
    }

    var mSetOne: Set<Int> = {1, 2, 3} //define writable Set mSetOne={1, 2, 3}
    let mSetTwo: Set<Int> = {3, 4, 5} //define read-only Set mSetTwo={3, 4, 5}
    var mSetThree = mSetOne.intersect(mSetTwo)  //mSetThree={3}
    mSetThree = mSetOne + mSetTwo  //mSetThree={1,2,3,4,5}, duplicate element are removed
    mSetThree = mSetThree - mSetOne  //mSetThree={4,5}
    mSetThree.add(3)  //mSetThree={3,4,5}
    mSetThree.add(6)  //mSetThree={3,4,5,6}
    mSetThree.remove(3)  //mSetThree={4,5,6}
    var mSetThreeLength = mSetThree!=mSetTwo ? mSetThree.length:mSetTwo.length   //mSetThreeLength=3
    mSetThree.contains(4)   //return true
    mSetThree.isEmpty()   //return false
    for item in mSetThree {  //visit all elements in mSetThree
        print(item)                           //output: 4 5 6
    }

    var mMapOne: Map<Int, String> = {1 -> "Aa", 2 -> "Bb", 3 -> "Cc"} //define writable Map mMapOne
    let mMapTwo: Map<Int, String> = {3 -> "Cc", 4 -> "Dd", 5 -> "Ee"} //define read-only Map mMapTwo
    var mMapThree: Map<Int, String>
    mMapThree = mMapOne.intersect(mMapTwo)  //mMapThree={3 -> "Cc"}
    mMapThree = mMapOne + mMapTwo
    mMapThree = mMapThree - {1, 2, 3}  //mMapThree={4 -> "Dd", 5 -> "Ee"}
    mMapThree.add(3 -> "Cc")  //mMapThree={3 -> "Cc", 4 -> "Dd", 5 -> "Ee"}
    mMapThree.add(6 -> "Ff")  //mMapThree={3 -> "Cc", 4 -> "Dd", 5 -> "Ee", 6 -> "Ff"}
    mMapThree.remove(3 -> "Cc")  //mMapThree={4 -> "Dd", 5 -> "Ee", 6 -> "Ff"}
    var mMapThreeLength = mMapThree!=mMapTwo ? mMapThree.length:mMapTwo.length   //mMapThreeLength=3
    mMapThree.containsKey(4)   //return true
    mMapThree.containsValue("Ff")   //return true
    mMapThree.contains(7 -> "Gg")   //return false
    mMapThree.isEmpty()   //return false
    for item in mMapThree {   //visit all elements in mMapThree
        print(item)                           //output: 4=Dd 5=Ee 6=Ff
    }

//from comments.char
	//oneline comments

    /*multiline comments
    */

    /*
    /*nested comments*/
    */

    /**
    doc comments
    */

//from comparisonOperators.char
	expr1 == expr2  //Equal to
    expr1 != expr2  //Not equal to
    expr1 < expr2  //Less than
    expr1 <= expr2  //Less than or equal to
    expr1 > expr2  //Greater than
    expr1 >= expr2  //Greater than or equal to

//from compoundAssignOperators.char
	a += b  //a = a + b
    a -= b  // a = a - b
    a *= b  //a = a * b
    a /= b  // a = a / b
    a %= b  // a = a % b
    a **= b  // a = a ** b
    a &&= b  //a = a && b
    a ||= b  // a = a || b
    a &= b  //a = a & b
    a |= b  // a = a | b
    a ^= b  //a = a ^ b
    a <<= b  // a = a << b
    a >>= b  // a = a >> b

//from conditionalOperator
    var i: Int = 15
    var j: Int = 20
    var k: Int = i>j ? i : j //k=max(i,j)=20

//from controlTransfer.char
//break statement
    var index:Int = 0
    while  (index < 50){
        index = index + 1
        if ((index % 4 == 0) && (index % 6 == 0)){
            print("\(index) is divisible by both 4 and 6")
            break
        }
    }
//break statement
    var index:Int = 0
    for (var i:Int = 0; i < 5; i++){
        while (index < 50){
            index = index + 1
            if ((index % 4 == 0) && (index % 6 == 0)){
                print("\(index) is divisible by both 4 and 6")
                break
            }
        }
    }

//continue statement
    var index:Int = 0
    while  (index < 50){
        index = index + 1
        if ((index % 4 == 0) && (index % 6 == 0)){
            print("\(index) is divisible by both 4 and 6")
            continue
        }
        print("\(index) is not what we want")
    }

//fallthrough statement
    let language = "Java"
    var output = "The language "
    var result: String = match language {
        with "Java" | "Kotlin" => output += language +" is a programing language and"
        fallthrough
        with _ => output += " object-oriented."
    }
    print(output) // Prints "Java is a programing language and object-oriented."

//return statement
    func equal(a : Int, b: Int) {
        if (a == b) {
            print("a is equal to b")
            return
        }
        else {
            print("a is not equal to b")
        }
    }
    func larger(a : Int, b: Int) : Int{
        if (a >= b) {
            return a
        }
        else {
            return b
        }
    }

//throw statement
    func div(a : Int, b: Int) : Int{
        if (b != 0) {
            return a/b
        }
        else {
            throw Exception()
        }
    }

//from enum
    enum TimeUnit { Year
        | Month
        | Day
        | Hour
    }
    enum TimeUnit {
        Year, Month, Day, Hour
    }

    enum TimeUnit: Int { Year = 0
        | Month = 1
        | Day = 2
        | Hour = 3
    }

    enum TimeUnit: Float { Year = 12.0
        | Month = 30.0
        | Day = 24.0
        | Hour = 1.0
    }

    enum TimeUnit: Rune { Year = 'Y'
        | Month = 'M'
        | Day = 'D'
        | Hour = 'H'
    }

    enum TimeUnit: String { Year = "year"
        | Month = "month"
        | Day = "day"
        | Hour = "hour"
    }

    enum TimeUnit: Int {//with Year=0,Month=1,Day=2,Hour=3
        Year, Month, Day, Hour
    }

    enum TimeUnit: String {//with Year="year",Month="month",Day="day",Hour="hour"
        Year, Month, Day, Hour
    }

//define associated values with names
    enum TimeUnit { Year(year: Int)
        | Month(year: Int, month: Float)
        | Day(year: Int, month: Float, day: Float)
        | Hour(year: Int, month: Float, day: Float, hour: Float)
    }

//define associated values without names
    enum TimeUnit { Year(Int)
        | Month(Int, Float)
        | Day(Int, Float, Float)
        | Hour(Int, Float, Float, Float)
    }

//define associated values and raw values
    enum TimeUnit: Int { Year(year: Int)=1
        | Month(year: Int, month: Float)=2
        | Day(year: Int, month: Float, day: Float)=3
        | Hour(year: Int, month: Float, day: Float, hour: Float)=4
    }

    main(args: Array<String>) {
        var time = TimeUnit(rawValue=0) // time = TimeUnit.Year
        time = TimeUnit(rawValue=1) // time = TimeUnit.Month

        var time = Year
        time = Month
        var howManyHours: Int = match time {
            with Year => 365*24
            with Month => 30*24
            with Day => 24
            with Hour => 1
        }

        let months = Month(1, 1.5)
        var howManyHours: Float = match months {
            with Year(y) => y*365*24
            with Month(y,m) => y*365*24+m*30*24
            with Day(y,m,d) => y*365*24+m*30*24+d*24
            with Hour(y,m,d,h) => y*365*24+m*30*24+d*24+h
        }
    }

//from ifExpression.char
	let x:Int = 2
/*  if x > 0 { //error, must be (x > 0)
        print("x is bigger than 0")
    }
*/
/*
    if (x > 0)  //error, print statement must be put into {}
        print("x is bigger than 0")
*/
    if (x > 0) {
        print("x is bigger than 0")
    }
    else if (x<0){
        print("x is lesser than 0")
    }
    else {
        print("x equals to 0")
    }

//from incrAndDecr.char
	var i: Int = 5
    var j: Int = 6
    ++i //i=6
    i++ //i=7
    --i //i=6
    i-- //i=5

//from letAndVar.char
	let width: Int
    width = 10
    let length: Int = 20
    var width2: Int =10
    var width3: Int
    width3 = 10
    var width4 = 10
)"
        // break long string into two, string literal longer than 16380
        // characters will be truncated in visual studio toolchain
        R"(
//from literals
	0b0001_1000 //binary
    0o30 //octal
    24 //decimal
    0x18 //hexadecimal

	3.14 //decimal float 3.14
    2.4e-1 //decimal float 0.24
    2e3 //decimal float 8
    .8 //decimal float 0.8
    .123e2 //decimal float 12.3
    0x1.1p0 //hexadecimal float 1.0625(decimal value)
    0x1p2 //hexadecimal float 4(decimal value)
    0x.2p4 //hexadecimal float 2(decimal value)

	'S'
    '5'
    ' ' //blank

	true
	false

//from logicalOperators.char
	!expr  //logical NOT
    expr1 && expr2  //logical AND
    expr1 || expr2  //logical OR

//from loopExpression.char
	let  arr[] = [0, 1, 2, 3, 4]
    for (var i:Int = 0; i < 5; i++){
        print(arr[i])
    }

    let  arr[] = [0, 1, 2, 3, 4]
    let i = 0
    for (; i < 5; i++){ //forInit is ommited
        print(arr[i]);
    }

    let  arr[] = [0, 1, 2, 3, 4]
    let i = 0
    for (; ; i++){ //forInti and condition are ommited
        if (i<5){
            print(arr[i]);
        }
        else{
            break
        }
    }

    let  arr[] = [0, 1, 2, 3, 4]
    let i = 0
    for (; ; ){ //forInti, condition and expression are ommited
        if (i<5){
            print(arr[i]);
            i++
        }
        else{
            break
        }
    }

    let  arr1[] = [0, 1, 2, 3, 4]
    let  arr2[] = [10, 11, 12, 13, 14]
    for (var i:Int = 0, var j:Int = 0; i < 5 && j < 5; i++, j++){
        print(arr[i]);
        print(arr[j]);
    }

    let  arr[] = [0, 1, 2, 3, 4]
    for (i in arr){
        print(i)
    }
    let  intList: List<Int> = {0, 1, 2, 3, 4}
    for (item in intList){
        print(item)
    }
    let  intSet: Set<Int> = {0, 1, 2, 3, 4}
    for (item in intSet){
        print(item)
    }
    let  map: Map<Int,String> = {0 -> "a", 1 -> "b", 2 -> "c", 3 -> "d", 4 -> "e"}
    for ((number,char) in map){
        print(number)
        print(char)
    }
    let intRange: ClosedRange = 0:10
    for (number in intRange){
        print(number)
    }

	var hundred = 0
    //while statement
    while  (hundred < 100){
        hundred++
    }

    var hundred = 0
    //do-while statement
    do{
        hundred++
}while (hundred < 100)

//from optionalType.char
	let optionalInt: Int? = nil
    var optionalChar: Rune? = 'm'
    var optionalBoolean: Boolean?
    optionalBoolean = nil
    let arrayZero[2]: String? = [nil, nil]

    let width: Int? = 10
    let height: Int? = nil
    var widthVal: Int
    var heightVal: Int
    if (width != nil){
        widthVal = width!
    }
    else{
        widthVal = 0
    }
    if (height.hasValue()){
        heightVal = height.get()
    }
    else{
        heightVal = 0
    }

//from patternMatching
//constant pattern
    let score: Int = 90
    let PASS = 60
    let FULL = 60
    var scoreResult: String = match score {
        with 0 => "zero"
        with 10 | 20 | 30 | 40 | 50 => "fail"
        with $PASS => "pass"
        with 70 | 80 => "good"
        with 90 | $FULL => "excellent"
        with _ =>
    }

    let score: Int = 90
    var scoreResult: String = match score {
        with 60 =>  "pass"
        with 70 | 80 =>  "good"
        with 90 | 100 =>  "excellent"
        with _ =>  "fail" //used for default with
    }

    let scoreTuple = ("Ark-Lang",90)
    var scoreResult: String = match scoreTuple {
        with (_, 60) =>  "pass"
        with (_, 70 | 80) =>  "good"
        with (_, 90 | 100) =>  "excellent"
        with (_, _) =>  "fail"
    }

//variable pattern1
    let score: Int = 90
    let PASS = 60
    let Full = 100
    let good = 70
    var scoreResult: String = match score {
        with $PASS =>  "pass" // constant pattern
        with $Full =>  "Full" // ok, but not recommend
        with good =>  "good"
    }

//variable pattern2
    let score: Int = 90
    var scoreResult: String = match score {
        with 60 =>  "pass"
        with 70 | 80 =>  "good"
        with 90 | 100 =>  "excellent"
        with failScore =>  "failed with "+failScore.toString()
    }

    let scoreTuple = ("Ark-Lang",90)
    var scoreResult: String = match scoreTuple {
        with (who, 60) =>  who+" passed"
        with (who, 70 | 80) =>  who+" got good"
        with (who, 90 | 100) =>  who+" got excellent"
        with (who, failScore) =>  who+" failed with "+failScore.toString()
    }

//value-binding pattern
    let scoreTuple = (("Allen",90),("Jack",60))
    var scoreResult: String = match scoreTuple {
        with ((_, _), secondStudent @ (who,score)) => "The info. of the second student is "+ secondStudent
    }

//tuple pattern
    let scoreTuple = ("Allen",90)
    var scoreResult: String = match scoreTuple {
        with ("Bob",90) =>  "Bob got 90"
        with ("Allen",score) =>  "Allen got "+score.toString()
        with (_,100) =>  "someone got 100"
        with (_,_) =>  ""
    }

//sequence pattern
    let scoreList: List<Int> = {50,60,70,80,90} // sorted array
    var scoreResult: String = match scoreList {
        with List{50,...} =>  "the lowest score is 50"
        with List{...,90} =>  "the highes score is 90"
        with List{low,...,high} =>  "the lowest score is "+low.toString()+", the highes score is "+high.toString()
    }

//map pattern
    let scoreMap: Map<String,Int> = {"E" -> 50, "D" -> 60, "C" -> 70, "B" -> 80, "A" -> 90}
    var scoreResult: String = match scoreMap {
        with Map{"A" -> score} => "the highest score is "+score.toString()
        with Map{"E" -> score} => "the lowest score is "+score.toString()
        with Map{"A" -> score0, "B" -> score1, "C" -> score2, "D" -> score3} =>
        "passed scores are "+score0.toString()+", "+score1.toString()+", "+score2.toString()+", "+score3.toString()
        //illegal collection patterns:
        //with Map{a} =>  "..." // illegal map pattern
        //with Map{a -> 50} =>  "..." // illegal map pattern
    }

//in pattern
    let scorePass: Set<Int> = {60, 70, 80, 90, 100}
    let score = 50
    var scoreResult: String = match score {
        with in $scorePass =>  "pass"
        with in (10,20,30) =>  "very low score"
        with in [40,50] =>  "low score"
    }

    //type pattern
    open class Point{
        var x: Int
        var y: Int
        init(x: Int, y: Int){
            this.x = x
            this.y = y
        }
    }
    class ColoredPoint: Point{
        var color: String
        init(x: Int, y: Int, color: String){
            this.x = x
            this.y = y
            this.color = color
        }
    }
    let normalPo = Point(5,10)
    let colorPo = ColoredPoint(8,24,"red")
    var rectangleArea1: Int = match normalPo {
        with _ : Point => normalPo.x*normalPo.y
                with _ => 0
    }
    var rectangleArea2: Int = match colorPo {
        with cp : Point => cp.x*cp.y //cp.color is not existed
                with _ => 0
    }

//enum pattern
//sum type
    enum TimeUnit_1 { Year
        | Month
        | Day
        | Hour
    }
//product type
    enum TimeUnit_2 { Month(year: Float, month: Float)
    }
//sum type that is consist of product type
    enum TimeUnit { Year(year: Float)
        | Month(year: Float, month: Float)
        | Day(year: Float, month: Float, day: Float)
        | Hour(year: Float, month: Float, day: Float, hour: Float)
    }

//sum type
    let tu = Year
    var tuString: String = match tu {
        with Year => "use Year as timeUnit"
        with Month => "use Month as timeUnit"
        with Day => "use Day as timeUnit"
        with Hour => "use Hour as timeUnit"
    }
//sum type that is consist of product type
    let oneYear = Year(1.0)
    var howManyHours: Float = match oneYear {
        with Year(y) => y*365*24
        with Month(y,m) => y*365*24+m*30*24
        with Day(y,m,d) => y*365*24+m*30*24+d*24
        with Hour(y,m,d,h) => y*365*24+m*30*24+d*24+h
    }

//pattern guards
    let oneYear = Year(1.0) //for solution 1
    var howManyHours: Float = match oneYear {
        with Year(y) if y > 0 => y*365*24
        with Year(y) if y <= 0 => 0.0
        with Month(y,m) if y > 0 && m > 0 => y*365*24+m*30*24
        with Day(y,m,d) if y > 0 && m > 0 && d > 0 => y*365*24+m*30*24+d*24
        with Hour(y,m,d,h) if y > 0 && m > 0 && d > 0 && h > 0 => y*365*24+m*30*24+d*24+h
    }

//other places using pattern matching
//using tuple pattern
    let point = (10,20)
    let (x,y) = point
//using enum pattern
    let hours = Hour(1.0, 2.4, 3.5, 4.6)
    let Hour(year, month, day, hour) = hours

//using pattern in for-in loop
    main() {
        let scoreList: List<Int> = {10,20,30,40,50,60,70,80,90,100}
        for item in scoreList { // variable&in pattern
            print(item)
        }
        for item in scoreList if item > 50 { // variable&in pattern with guards
        print(item)
    }
        for _ in scoreList { // wildcard&in pattern
            print(item)
        }
    }

//using match expression instead of if-else if
    let score = 80
    var result: String = match {
        with score < 60 => "fail"
        with score < 70 => "pass"
        with score < 90 => "good"
        with _ => "excellent"
    }

//from positiveAndNegativeOps
    var i: Int = 5
    var j: Int = +i // j=5
    var k: Int = -i // k=-5

//from primaryTypes.char
	let int8bit: Int8 = 24  //the range of Int8 is: -2**7 ~ 2**7-1 (-128 ~ 127)
    let int16bit: Int16 = 529  //the range of Int16 is: -2**15 ~ 2**15-1 (-32768 ~ 32767)
    let intShort: Short = 529  //the range of Short(Int16) is: -2**15 ~ 2**15-1 (-32768 ~ 32767)
    let int32bit: Int32 = 58041  //the range of Int32 is: -2**31 ~ 2**31-1 (-2147483648~2147483647)
    let intInt: Int = 58041  //the range of Int(Int32) is: -2**31 ~ 2**31-1 (-2147483648~2147483647)
    let int64bit: Int64 = 6000000612
    let intLong: Long = 6000000612

    let uint8bit: UInt8 = 128  //the range of UInt8 is: 0 ~ 2**8-1 (0 ~ 255)
    let uint16bit: UInt16 = 529  //the range of UInt16 is: 0 ~ 2**16-1 (0 ~ 65535)
    let uint32bit: UInt32 = 58041  //the range of UInt32 is: 0 ~ 2**32-1 (0~4294967295)
    let uintInt: UInt = 58041  //the range of UInt(UInt32) is: 0 ~ 2**32-1 (0~4294967295)
    let uint64bit: UInt64 = 6000000612  //the range of UInt64 is: 0 ~ 2**64-1 (0~18446744073709549615)

    let float16bit: Float16 = 3.1415E0
    let float32bit: Float32 = 1.234E10
    let floatfloat: Float = 1.234E10
    let float64bit: Float64 = 1.234E100
    let floatdouble: Double = 1.234E100

    let char: Rune = 'a'

    let booltrue: Boolean = true
    let boolfalse: Boolean = false

//from range.char
	let closedRange: ClosedRange = 0:10 // define closed range [1,10]
    let oneSideRange: OneSideRange = 1: // define one side range [1, 2147483647]
    for (index in 0:3) { // anonymous closed range is used
        array[index]=0 // array[0]=array[1]= array[2]= array[3]=0
    }
    let closedRange1: ClosedRange = 0:10
    let closedRange2: ClosedRange = 1:10
    closedRange1 == closedRange2 ? closedRange1:closedRange2 //return closedRange2
    closedRange1.length //return 11
    closedRange2.length //return 10
    closedRange2.contains(0) //return false
    closedRange1.isEmpty() //return false
    let array[8]:Int = [1,2,3,4,5,6,7,8]
    for (number in array[0:]){ //anonymous one side range is used
        print(number)  //output: 1 2 3 4 5 6 7 8
    }

//from semicolon.char
    let width:Int = 32
    let length:Int = 1024;

    let width2:Int = 32; let length2:Int=1024

//from simpleAndCompoundStatements
    var x :Int = 5
    x+=10
    x+=20; x+=30; x+=40
             //null statement
    ;        //null statement
    while(read(i) && i <= 10){ //read a number that greater than 10
        ;    //null statement
    }
    var x = 24
    if ( x > 0) {
        x = 1
        print("x is bigger than 0, let it be 1")
    }
    else {
        x = 0
        print("x is not bigger than 0, let it be 0")
    }

//from string.char
	let string1: String = "Hello, "
    let string2 = "Rune"
    var string3 = string1 + string2
    print(string3)  //output: Hello, Rune
    print(string3.length)  //output: 11

//from tuple.char
	let tuplePIE: Tuple<Float, String> = (3.14, "PIE") //define read-only Tuple tuple1=(3.14, "PIE")
    var Point: Tuple<Float, Float> //define writable Tuple Point
    Point = (2.4, 3.5) //initialization
    var (x, y) = Point // define an anonymous tuple
    var point1: Tuple<Float, Float> = (2.4, 3.5)
    var point2: Tuple<Float, Float> = (8, 10)
    point1 == point2 ? point1:point2 //return point2
    point2.set(0, 10) //point2=(10,10)
    var rectangleArea = point2.get(0)*point2.get(1) //rectangleArea=100
    point2.length //return 2
    point2.contains(100) //return false
    point2.isEmpty() //return false

//from typeAlias
    type  pointHas5dims = Tuple<Float, Float, Float, Float, Float>
    type families = Set<People>

//from typeCast
	var intNumber: Int = 1024
    intNumber.toInt8()
    intNumber.toInt16()
    intNumber.toShort()
    intNumber.toInt64()
    intNumber.toLong()
    intNumber.toFloat16()
    intNumber.toFloat32()
    intNumber.toFloat()
    intNumber.toFloat64()
    intNumber.toDouble()
    intNumber.toUInt8()
    intNumber.toUInt16()
    intNumber.toUInt32()
    intNumber.toUInt()
    intNumber.toUInt64()
    intNumber.toChar()

    var floatNumber: Float = 10.24
	floatNumber.toInt8()
    floatNumber.toInt16()
    floatNumber.toShort()
    floatNumber.toInt32()
	floatNumber.toInt()
    floatNumber.toInt64()
    floatNumber.toLong()
    floatNumber.toFloat16()
    floatNumber.toFloat64()
    floatNumber.toDouble()
    floatNumber.toUInt8()
    floatNumber.toUInt16()
    floatNumber.toUInt32()
    floatNumber.toUInt()
    floatNumber.toUInt64()

	var character: Rune = 24
	floatNumber.toInt8()
    floatNumber.toInt16()
    floatNumber.toShort()
    floatNumber.toInt32()
	floatNumber.toInt()
    floatNumber.toInt64()
    floatNumber.toLong()
    floatNumber.toUInt8()
    floatNumber.toUInt16()
    floatNumber.toUInt32()
    floatNumber.toUInt()
    floatNumber.toUInt64()

//from UnitType
    func helloArkLang1(ArkLang: String) : Unit { //return Unit
        return a // error, should be return Unit
    }
    func helloArkLang2(ArkLang: String) {
        return a // ok, the return type of helloArkLang2 is the type of a
    }
    func helloArkLang3(ArkLang: String) {
        return // the return type of helloArkLang3 is Unit
    }
    func helloArkLang4(ArkLang: String) {
    //...
    //if no return statement, the return type of helloArkLang4 is Unit
    }

//from unsafeOps
    let a: Int = 2147483647 //INT_MAX
    let b: Int = 1
	let c: Int = b + a //error, overflow is reported by compiler
	let d: Int = b +& a //ok, but c=-2147483648
    a +& b  //unsafe add
    a -& b  //unsafe sub
    a *& b  //unsafe multi
    a **& b  //unsafe power
    a +&= b  //unsafe compoundAssignOperators
    a -&= b
    a *&= b
    a **&= b
}
)";

    std::unique_ptr<Lexer> lexer;
    SourceManager sm;
    DiagnosticEngine diag{}; // When constructor of DiagnosticEngine update,
                             // maybe should update here.
};

TEST_F(TotalTest, AllTokens)
{
    for (;;) {
        Token tok = lexer->Next();
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}
