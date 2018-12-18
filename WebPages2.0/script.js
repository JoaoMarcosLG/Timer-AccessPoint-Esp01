var previous_length = 0;

function check_time(element) {
    if(element.value.length == 2 && element.value.length > previous_length) {
        element.value += ':';
    }
    previous_length = element.value.length;
}