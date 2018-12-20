var previous_length = {};
function check_time(element, repeat) {
    id = element.name;
    if(!(id in previous_length)) { previous_length[id] = 0; }

    for(let i=0; i < repeat; i++) {
        if(element.value.length == ((2*i)+2+(i*1)) && element.value.length > previous_length[id]) {
            element.value += ':';
        }
    }

    previous_length[id] = element.value.length;
}

var time_count = 1;
function add_cell() {
    let new_cell = document.createElement('div');
    new_cell.className = 'time-input';
    new_cell.innerHTML = `<label>Horário ${++time_count}:</label><input name="h${time_count}" type="text" placeholder="00:00" autocomplete="off" maxlength="5" onkeyup="check_time(this, 1)" required><button type="button" class="btn btn-success" style="height: 42px" onclick="add_cell()">+</button>`;
    document.getElementById('times-container').appendChild(new_cell);
    
    let previous_button = new_cell.previousElementSibling.getElementsByTagName('button')[0];
    previous_button.className = 'btn btn-danger';
    previous_button.innerHTML = 'x';
    previous_button.onclick = function() { remove_cell(previous_button) };
}

function remove_cell(element) {
    let times_container = document.getElementById('times-container');
    times_container.removeChild(element.parentNode);
    let count = 0;
    for(let i=0; i < times_container.children.length; i++) {
        times_container.children[i].firstChild.innerText = `Horário ${++count}:`;
    }
    time_count = count;
}