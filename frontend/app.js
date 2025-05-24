const form = document.getElementById('studentForm');
const messageDiv = document.getElementById('message');
const studentList = document.getElementById('studentList');

// Display feedback message
function showMessage(message, isError = false) {
    messageDiv.textContent = message;
    messageDiv.className = isError ? 'error' : 'success';
    messageDiv.classList.add(isError ? 'error' : 'success');
    messageDiv.style.display = 'block';
}

// Add student via POST request
async function addStudent(id, name, course) {
    try {
        const response = await fetch('http://localhost:8080/api/addStudent', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: `id=${id}&name=${encodeURIComponent(name)}&course=${encodeURIComponent(course)}`
        });
        const text = await response.text();
        return { success: response.ok, message: text };
    } catch (error) {
        return { success: false, message: 'Server error. Please try again.' };
    }
}

// Fetch student list
async function fetchStudents() {
    try {
        const res = await fetch('http://localhost:8080/api/students');
        if (!res.ok) throw new Error();
        return await res.json();
    } catch {
        showMessage('Failed to load students.', true);
        return [];
    }
}

// Render student table
function displayStudents(students) {
    if (!students.length) {
        studentList.innerHTML = '<p>No students found.</p>';
        return;
    }

    const table = document.createElement('table');
    table.innerHTML = `
        <thead>
            <tr><th>ID</th><th>Name</th><th>Course</th></tr>
        </thead>
        <tbody>
            ${students.map(s => `
                <tr>
                    <td>${s.id}</td>
                    <td>${s.name}</td>
                    <td>${s.course}</td>
                </tr>
            `).join('')}
        </tbody>
    `;

    studentList.innerHTML = '';
    studentList.appendChild(table);
}

// Form submit handler
form.addEventListener('submit', async e => {
    e.preventDefault();
    const id = document.getElementById('id').value.trim();
    const name = document.getElementById('name').value.trim();
    const course = document.getElementById('course').value.trim();

    if (!id || !name || !course) {
        showMessage('All fields are required.', true);
        return;
    }

    const result = await addStudent(id, name, course);
    showMessage(result.message, !result.success);

    if (result.success) {
        form.reset();
        const students = await fetchStudents();
        displayStudents(students);
    }
});

// Load students on page load
fetchStudents().then(displayStudents);
