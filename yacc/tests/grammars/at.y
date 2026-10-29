
%token hr24clock_hr_min
%token hr24clock_hour
/*
  An hr24clock_hr_min is a one, two, or four-digit number. A one-digit
  or two-digit number constitutes an hr24clock_hour. An hr24clock_hour
  may be any of the single digits [0,9], or may be double digits, ranging
  from [00,23]. If an hr24clock_hr_min is a four-digit number, the
  first two digits shall be a valid hr24clock_hour, while the last two
  represent the number of minutes, from [00,59].
*/

%token wallclock_hr_min
%token wallclock_hour
/*
  A wallclock_hr_min is a one, two-digit, or four-digit number.
  A one-digit or two-digit number constitutes a wallclock_hour.
  A wallclock_hour may be any of the single digits [1,9], or may
  be double digits, ranging from [01,12]. If a wallclock_hr_min
  is a four-digit number, the first two digits shall be a valid
  wallclock_hour, while the last two represent the number of
  minutes, from [00,59].
*/

%token minute
/*
  A minute is a one or two-digit number whose value can be [0,9]
  or [00,59].
*/

%token day_number
/*
  A day_number is a number in the range appropriate for the particular
  month and year specified by month_name and year_number, respectively.
  If no year_number is given, the current year is assumed if the given
  date and time are later this year. If no year_number is given and
  the date and time have already occurred this year and the month is
  not the current month, next year is the assumed year.
*/

%token year_number
/*
  A year_number is a four-digit number representing the year A.D., in
  which the at_job is to be run.
*/

%token inc_number
/*
  The inc_number is the number of times the succeeding increment
  period is to be added to the specified date and time.
*/

%token timezone_name
/*
  The name of an optional timezone suffix to the time field, in an
  implementation-defined format.
*/

%token month_name
/*
  One of the values from the mon or abmon keywords in the LC_TIME
  locale category.
*/

%token day_of_week
/*
  One of the values from the day or abday keywords in the LC_TIME
  locale category.
*/

%token am_pm
/*
  One of the values from the am_pm keyword in the LC_TIME locale
  category.
*/

%start timespec
%%
timespec    : time
            | time date
            | time increment
            | time date increment
            | nowspec
            ;

nowspec     : "now"
            | "now" increment
            ;

time        : hr24clock_hr_min
            | hr24clock_hr_min timezone_name
            | hr24clock_hour ":" minute
            | hr24clock_hour ":" minute timezone_name
            | wallclock_hr_min am_pm
            | wallclock_hr_min am_pm timezone_name
            | wallclock_hour ":" minute am_pm
            | wallclock_hour ":" minute am_pm timezone_name
            | "noon"
            | "midnight"
            ;

date        : month_name day_number
            | month_name day_number "," year_number
            | day_of_week
            | "today"
            | "tomorrow"
            ;

increment   : "+" inc_number inc_period
            | "next" inc_period
            ;

inc_period  : "minute" | "minutes"
            | "hour" | "hours"
            | "day" | "days"
            | "week" | "weeks"
            | "month" | "months"
            | "year" | "years"
            ;
